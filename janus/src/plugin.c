/*****************************************************************************
#                                                                            #
#    uStreamer - Lightweight and fast MJPEG-HTTP streamer.                   #
#                                                                            #
#    Copyright (C) 2018-2024  Maxim Devaev <mdevaev@gmail.com>               #
#                                                                            #
#    This program is free software: you can redistribute it and/or modify    #
#    it under the terms of the GNU General Public License as published by    #
#    the Free Software Foundation, either version 3 of the License, or       #
#    (at your option) any later version.                                     #
#                                                                            #
#    This program is distributed in the hope that it will be useful,         #
#    but WITHOUT ANY WARRANTY; without even the implied warranty of          #
#    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the           #
#    GNU General Public License for more details.                            #
#                                                                            #
#    You should have received a copy of the GNU General Public License       #
#    along with this program.  If not, see <https://www.gnu.org/licenses/>.  #
#                                                                            #
*****************************************************************************/


#include <stdatomic.h>
#include <stdlib.h>
#include <inttypes.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <assert.h>

#include <sys/mman.h>
#include <sys/stat.h>

#include <pthread.h>
#include <jansson.h>
#include <janus/plugins/plugin.h>
#include <janus/rtp.h>
#include <janus/rtcp.h>
#include <alsa/asoundlib.h>

#include "uslibs/types.h"
#include "uslibs/const.h"
#include "uslibs/errors.h"
#include "uslibs/tools.h"
#include "uslibs/threading.h"
#include "uslibs/list.h"
#include "uslibs/ring.h"
#include "uslibs/memsinksh.h"
#include "uslibs/tc358743.h"

#include "const.h"
#include "logging.h"
#include "client.h"
#include "au.h"
#include "acap.h"
#include "rtp.h"
#include "rtpv.h"
#include "rtpa.h"
#include "memsinkfd.h"
#include "config.h"

static us_config_s		*_g_config = NULL;
static const useconds_t	_g_watchers_polling = 100000;

static us_janus_client_s	*_g_clients = NULL;
static janus_callbacks		*_g_gw = NULL;
static us_ring_s			*_g_video_ring = NULL;
static us_rtpv_s			*_g_rtpv = NULL;
static us_rtpa_s			*_g_rtpa = NULL; // Also indicates "audio capture is available"

static pthread_t		_g_video_rtp_tid;
static atomic_bool		_g_video_rtp_tid_created = false;
static pthread_t		_g_video_sink_tid;
static atomic_bool		_g_video_sink_tid_created = false;
static pthread_t		_g_acap_tid;
static atomic_bool		_g_acap_tid_created = false;
static pthread_t		_g_aplay_tid;
static atomic_bool		_g_aplay_tid_created = false;

static pthread_mutex_t	_g_video_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t	_g_acap_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t	_g_aplay_lock = PTHREAD_MUTEX_INITIALIZER;
static atomic_bool		_g_ready = false;
static atomic_bool		_g_stop = false;
static atomic_bool		_g_has_watchers = false;
static atomic_bool		_g_has_listeners = false;
static atomic_bool		_g_has_speakers = false;
static atomic_bool		_g_key_required = false;


#define _LOCK_VIDEO		US_MUTEX_LOCK(_g_video_lock)
#define _UNLOCK_VIDEO	US_MUTEX_UNLOCK(_g_video_lock)

#define _LOCK_ACAP		US_MUTEX_LOCK(_g_acap_lock)
#define _UNLOCK_ACAP	US_MUTEX_UNLOCK(_g_acap_lock)

#define _LOCK_APLAY		US_MUTEX_LOCK(_g_aplay_lock)
#define _UNLOCK_APLAY	US_MUTEX_UNLOCK(_g_aplay_lock)

#define _LOCK_ALL		{ _LOCK_VIDEO; _LOCK_ACAP; _LOCK_APLAY; }
#define _UNLOCK_ALL		{ _UNLOCK_APLAY; _UNLOCK_ACAP; _UNLOCK_VIDEO; }

#define _READY			atomic_load(&_g_ready)
#define _STOP			atomic_load(&_g_stop)
#define _HAS_WATCHERS	atomic_load(&_g_has_watchers)
#define _HAS_LISTENERS	atomic_load(&_g_has_listeners)
#define _HAS_SPEAKERS	atomic_load(&_g_has_speakers)

#define _IF_DISABLED(...) { if (!_READY || _STOP) { __VA_ARGS__ } }


janus_plugin *create(void);


static void *_video_rtp_thread(void *arg) {
	(void)arg;
	US_THREAD_SETTLE("us_p_rtpv");
	atomic_store(&_g_video_rtp_tid_created, true);

	while (!_STOP) {
		const int ri = us_ring_consumer_acquire(_g_video_ring, 0.1);
		if (ri >= 0) {
			const us_frame_s *const frame = _g_video_ring->items[ri];
			_LOCK_VIDEO;
			const bool zero_playout_delay = (frame->gop == 0);
			us_rtpv_wrap(_g_rtpv, frame, zero_playout_delay);
			_UNLOCK_VIDEO;
			us_ring_consumer_release(_g_video_ring, ri);
		}
	}
	return NULL;
}

static void *_video_sink_thread(void *arg) {
	(void)arg;
	US_THREAD_SETTLE("us_p_vsink");
	atomic_store(&_g_video_sink_tid_created, true);

	us_frame_s *drop = us_frame_init();
	u64 frame_id = 0;
	int once = 0;

	while (!_STOP) {
		if (!_HAS_WATCHERS) {
			US_ONCE({ US_JLOG_INFO("video", "No active watchers, memsink disconnected"); });
			usleep(_g_watchers_polling);
			continue;
		}

		int fd = -1;
		us_memsink_shared_s *mem = NULL;

		const uz data_size = us_memsink_calculate_size(_g_config->video_sink_name);
		if (data_size == 0) {
			US_ONCE({ US_JLOG_ERROR("video", "Invalid memsink object suffix"); });
			goto close_memsink;
		}

		if ((fd = shm_open(_g_config->video_sink_name, O_RDWR, 0)) <= 0) {
			US_ONCE({ US_JLOG_PERROR("video", "Can't open memsink"); });
			goto close_memsink;
		}

		if ((mem = us_memsink_shared_map(fd, data_size)) == NULL) {
			US_ONCE({ US_JLOG_PERROR("video", "Can't map memsink"); });
			goto close_memsink;
		}

		once = 0;

		US_JLOG_INFO("video", "Memsink opened; reading frames ...");
		while (!_STOP && _HAS_WATCHERS) {
			const int waited = us_memsink_fd_wait_frame(fd, mem, frame_id);
			if (waited == 0) {
				const int ri = us_ring_producer_acquire(_g_video_ring, 0);
				us_frame_s *frame;
				if (ri >= 0) {
					frame = _g_video_ring->items[ri];
				} else {
					US_ONCE({ US_JLOG_PERROR("video", "Video ring is full"); });
					frame = drop;
				}

				const int got = us_memsink_fd_get_frame(fd, mem, frame, &frame_id, atomic_load(&_g_key_required));
				if (ri >= 0) {
					us_ring_producer_release(_g_video_ring, ri);
				}
				if (got < 0) {
					goto close_memsink;
				}

				if (ri >= 0 && frame->key) {
					atomic_store(&_g_key_required, false);
				}
			} else if (waited != US_ERROR_NO_DATA) {
				goto close_memsink;
			}
		}

	close_memsink:
		if (mem != NULL) {
			us_memsink_shared_unmap(mem, data_size);
			mem = NULL;
		}
		US_CLOSE_FD(fd);
		US_JLOG_INFO("video", "Memsink closed");
		sleep(1); // error_delay
	}

	us_frame_destroy(drop);
	return NULL;
}

static int _check_tc358743_acap(uint *hz) {
	int fd;
	if ((fd = open(_g_config->tc358743_dev_path, O_RDWR)) < 0) {
		US_JLOG_PERROR("acap", "Can't open TC358743 V4L2 device");
		return -1;
	}
	const int checked = us_tc358743_xioctl_get_audio_hz(fd, hz);
	if (checked < 0) {
		US_JLOG_PERROR("acap", "Can't check TC358743 audio state (%d)", checked);
		close(fd);
		return -1;
	}
	close(fd);
	return 0;
}

static void *_acap_thread(void *arg) {
	(void)arg;
	US_THREAD_SETTLE("us_p_ac");
	atomic_store(&_g_acap_tid_created, true);

	assert(_g_config->acap_dev_name != NULL);
	assert(_g_config->tc358743_dev_path != NULL);
	assert(_g_rtpa != NULL);

	int once = 0;

	while (!_STOP) {
		if (!_HAS_WATCHERS || !_HAS_LISTENERS) {
			usleep(_g_watchers_polling);
			continue;
		}

		uint hz = 0;
		us_acap_s *acap = NULL;

		if (_check_tc358743_acap(&hz) < 0) {
			goto close_acap;
		}
		if (hz == 0) {
			US_ONCE({ US_JLOG_INFO("acap", "No audio presented from the host"); });
			goto close_acap;
		}
		US_ONCE({ US_JLOG_INFO("acap", "Detected host audio"); });
		if ((acap = us_acap_init(_g_config->acap_dev_name, hz)) == NULL) {
			goto close_acap;
		}

		once = 0;

		while (!_STOP && _HAS_WATCHERS && _HAS_LISTENERS) {
			if (_check_tc358743_acap(&hz) < 0 || acap->pcm_hz != hz) {
				goto close_acap;
			}
			uz size = US_RTP_DATAGRAM_SIZE - US_RTP_HEADER_SIZE;
			u8 data[size];
			u64 pts;
			const int result = us_acap_get_encoded(acap, data, &size, &pts);
			if (result == 0) {
				_LOCK_ACAP;
				us_rtpa_wrap(_g_rtpa, data, size, pts);
				_UNLOCK_ACAP;
			} else if (result == -1) {
				goto close_acap;
			}
		}

	close_acap:
		US_DELETE(acap, us_acap_destroy);
		sleep(1); // error_delay
	}
	return NULL;
}

static void *_aplay_thread(void *arg) {
	(void)arg;
	US_THREAD_SETTLE("us_p_ap");
	atomic_store(&_g_aplay_tid_created, true);

	assert(_g_config->aplay_dev_name != NULL);

	int once = 0;

	while (!_STOP) {
		snd_pcm_t *dev = NULL;
		bool skip = true;

		while (!_STOP) {
			usleep((US_AU_FRAME_MS / 4) * 1000);

			us_au_pcm_s mixed = {0};
			_LOCK_APLAY;
			US_LIST_ITERATE(_g_clients, client, {
				us_au_pcm_s last = {0};
				do {
					const int ri = us_ring_consumer_acquire(client->aplay_pcm_ring, 0);
					if (ri >= 0) {
						const us_au_pcm_s *pcm = client->aplay_pcm_ring->items[ri];
						memcpy(&last, pcm, sizeof(us_au_pcm_s));
						us_ring_consumer_release(client->aplay_pcm_ring, ri);
					} else {
						break;
					}
				} while (skip && !_STOP);
				us_au_pcm_mix(&mixed, &last);
				// US_JLOG_INFO("++++++", "mixed %p", client);
			});
			_UNLOCK_APLAY;
			// US_JLOG_INFO("++++++", "--------------");

			if (skip) {
				static uint skipped = 0;
				if (skipped < (1000 / (US_AU_FRAME_MS / 4))) {
					++skipped;
					continue;
				} else {
					skipped = 0;
				}
			}

			if (!_HAS_WATCHERS || !_HAS_LISTENERS || !_HAS_SPEAKERS) {
				goto close_aplay;
			}

			if (dev == NULL) {
				int err = snd_pcm_open(&dev, _g_config->aplay_dev_name, SND_PCM_STREAM_PLAYBACK, 0);
				if (err < 0) {
					US_ONCE({ US_JLOG_PERROR_ALSA(err, "aplay", "Can't open PCM playback"); });
					goto close_aplay;
				}

				err = snd_pcm_set_params(dev, SND_PCM_FORMAT_S16_LE, SND_PCM_ACCESS_RW_INTERLEAVED,
					US_RTP_OPUS_CH, US_RTP_OPUS_HZ, 1 /* soft resample */, 50000 /* 50000 = 0.05sec */
				);
				if (err < 0) {
					US_ONCE({ US_JLOG_PERROR_ALSA(err, "aplay", "Can't configure PCM playback"); });
					goto close_aplay;
				}

				US_JLOG_INFO("aplay", "Playback opened, playing ...");
				once = 0;
			}

			if (dev != NULL && mixed.frames > 0) {
				snd_pcm_sframes_t frames = snd_pcm_writei(dev, mixed.data, mixed.frames);
				if (frames < 0) {
					frames = snd_pcm_recover(dev, frames, 1);
				} else {
					if (once != 0) {
						US_JLOG_INFO("aplay", "Playing resumed (snd_pcm_writei) ...");
					}
					once = 0;
					skip = false;
				}
				if (frames < 0) {
					US_ONCE({ US_JLOG_PERROR_ALSA(frames, "aplay", "Can't play to PCM playback"); });
					if (frames == -ENODEV) {
						goto close_aplay;
					}
					skip = true;
				} else {
					if (once != 0) {
						US_JLOG_INFO("aplay", "Playing resumed (snd_pcm_recover) ...");
					}
					once = 0;
					skip = false;
				}
			}
		}

	close_aplay:
		if (dev != NULL) {
			US_DELETE(dev, snd_pcm_close);
			US_JLOG_INFO("aplay", "Playback closed");
		}
	}
	return NULL;
}

static void _relay_rtp_clients(const us_rtp_s *rtp) {
	US_LIST_ITERATE(_g_clients, client, {
		us_janus_client_send(client, rtp);
	});
}

static void _alsa_quiet(const char *file, int line, const char *func, int err, const char *fmt, ...) {
	(void)file;
	(void)line;
	(void)func;
	(void)err;
	(void)fmt;
}

static int _plugin_init(janus_callbacks *gw, const char *config_dir_path) {
	// https://groups.google.com/g/meetecho-janus/c/xoWIQfaoJm8
	// sysctl -w net.core.rmem_default=500000 
	// sysctl -w net.core.wmem_default=500000 
	// sysctl -w net.core.rmem_max=1000000 
	// sysctl -w net.core.wmem_max=1000000

	US_JLOG_INFO("main", "Initializing PiKVM uStreamer plugin %s ...", US_VERSION);
	if (gw == NULL || config_dir_path == NULL || ((_g_config = us_config_init(config_dir_path)) == NULL)) {
		return -1;
	}
	_g_gw = gw;

	snd_lib_error_set_handler(_alsa_quiet);

	US_RING_INIT_WITH_ITEMS(_g_video_ring, 64, us_frame_init);
	_g_rtpv = us_rtpv_init(_relay_rtp_clients);
	if (_g_config->acap_dev_name != NULL && us_acap_probe(_g_config->acap_dev_name)) {
		_g_rtpa = us_rtpa_init(_relay_rtp_clients);
		US_THREAD_CREATE(_g_acap_tid, _acap_thread, NULL);
		if (_g_config->aplay_dev_name != NULL) {
			US_THREAD_CREATE(_g_aplay_tid, _aplay_thread, NULL);
		}
	}
	US_THREAD_CREATE(_g_video_rtp_tid, _video_rtp_thread, NULL);
	US_THREAD_CREATE(_g_video_sink_tid, _video_sink_thread, NULL);

	atomic_store(&_g_ready, true);
	return 0;
}

static void _plugin_destroy(void) {
	US_JLOG_INFO("main", "Destroying plugin ...");

	atomic_store(&_g_stop, true);
#	define JOIN(_tid) { if (atomic_load(&_tid##_created)) { US_THREAD_JOIN(_tid); } }
	JOIN(_g_video_sink_tid);
	JOIN(_g_video_rtp_tid);
	JOIN(_g_acap_tid);
	JOIN(_g_aplay_tid);
#	undef JOIN

	US_LIST_ITERATE(_g_clients, client, {
		US_LIST_REMOVE(_g_clients, client);
		us_janus_client_destroy(client);
	});

	US_RING_DELETE_WITH_ITEMS(_g_video_ring, us_frame_destroy);

	US_DELETE(_g_rtpa, us_rtpa_destroy);
	US_DELETE(_g_rtpv, us_rtpv_destroy);
	US_DELETE(_g_config, us_config_destroy);
}

static void _plugin_create_session(janus_plugin_session *session, int *err) {
	_IF_DISABLED({ *err = -1; return; });
	_LOCK_ALL;
	US_JLOG_INFO("main", "Creating session %p ...", session);
	us_janus_client_s *const client = us_janus_client_init(_g_gw, session);
	US_LIST_APPEND(_g_clients, client);
	atomic_store(&_g_has_watchers, true);
	_UNLOCK_ALL;
}

static void _plugin_destroy_session(janus_plugin_session* session, int *err) {
	_IF_DISABLED({ *err = -1; return; });
	_LOCK_ALL;
	bool found = false;
	bool has_watchers = false;
	bool has_listeners = false;
	bool has_speakers = false;
	US_LIST_ITERATE(_g_clients, client, {
		if (client->session == session) {
			US_JLOG_INFO("main", "Removing session %p ...", session);
			US_LIST_REMOVE(_g_clients, client);
			us_janus_client_destroy(client);
			found = true;
		} else {
			has_watchers = (has_watchers || atomic_load(&client->transmit));
			has_listeners = (has_listeners || atomic_load(&client->transmit_acap));
			has_speakers = (has_speakers || atomic_load(&client->transmit_aplay));
		}
	});
	if (!found) {
		US_JLOG_WARN("main", "No session %p", session);
		*err = -2;
	}
	atomic_store(&_g_has_watchers, has_watchers);
	atomic_store(&_g_has_listeners, has_listeners);
	atomic_store(&_g_has_speakers, has_speakers);
	_UNLOCK_ALL;
}

static json_t *_plugin_query_session(janus_plugin_session *session) {
	_IF_DISABLED({ return NULL; });
	json_t *info = NULL;
	_LOCK_ALL;
	US_LIST_ITERATE(_g_clients, client, {
		if (client->session == session) {
			info = json_string("session_found");
			break;
		}
	});
	_UNLOCK_ALL;
	return info;
}

static void _set_transmit(janus_plugin_session *session, const char *msg, bool transmit) {
	(void)msg;
	_IF_DISABLED({ return; });
	_LOCK_ALL;
	bool found = false;
	bool has_watchers = false;
	US_LIST_ITERATE(_g_clients, client, {
		if (client->session == session) {
			atomic_store(&client->transmit, transmit);
			// US_JLOG_INFO("main", "%s session %p", msg, session);
			found = true;
		}
		has_watchers = (has_watchers || atomic_load(&client->transmit));
	});
	if (!found) {
		US_JLOG_WARN("main", "No session %p", session);
	}
	atomic_store(&_g_has_watchers, has_watchers);
	_UNLOCK_ALL;
}

static void _plugin_setup_media(janus_plugin_session *session) { _set_transmit(session, "Unmuted", true); }
static void _plugin_hangup_media(janus_plugin_session *session) { _set_transmit(session, "Muted", false); }

static struct janus_plugin_result *_plugin_handle_message(
	janus_plugin_session *session, char *transaction, json_t *msg, json_t *jsep) {

	janus_plugin_result_type result_type = JANUS_PLUGIN_OK;
	char *result_msg = NULL;

	if (session == NULL || msg == NULL) {
		result_type = JANUS_PLUGIN_ERROR;
		result_msg = (msg ? "No session" : "No message");
		goto done;
	}

#	define PUSH_ERROR(x_error, x_reason) { \
			/*US_JLOG_ERROR("main", "Message error in session %p: %s", session, x_reason);*/ \
			json_t *m_event = json_object(); \
			json_object_set_new(m_event, "ustreamer", json_string("event")); \
			json_object_set_new(m_event, "error_code", json_integer(x_error)); \
			json_object_set_new(m_event, "error", json_string(x_reason)); \
			_g_gw->push_event(session, create(), NULL, m_event, NULL); \
			json_decref(m_event); \
		}

	json_t *const request = json_object_get(msg, "request");
	if (request == NULL) {
		PUSH_ERROR(400, "Request missing");
		goto done;
	}

	const char *const request_str = json_string_value(request);
	if (request_str == NULL) {
		PUSH_ERROR(400, "Request not a string");
		goto done;
	}
	// US_JLOG_INFO("main", "Message: %s", request_str);

#	define PUSH_STATUS(x_status, x_payload, x_jsep) { \
			json_t *const m_event = json_object(); \
			json_object_set_new(m_event, "ustreamer", json_string("event")); \
			json_t *const m_result = json_object(); \
			json_object_set_new(m_result, "status", json_string(x_status)); \
			if (x_payload != NULL) { \
				json_object_set(m_result, x_status, x_payload); \
			} \
			json_object_set_new(m_event, "result", m_result); \
			_g_gw->push_event(session, create(), NULL, m_event, x_jsep); \
			json_decref(m_event); \
		}

	if (!strcmp(request_str, "start")) {
		PUSH_STATUS("started", NULL, NULL);

	} else if (!strcmp(request_str, "stop")) {
		PUSH_STATUS("stopped", NULL, NULL);

	} else if (!strcmp(request_str, "watch")) {
		uint video_orient = 0;
		bool with_acap = false;
		bool with_aplay = false;
		{
			json_t *const params = json_object_get(msg, "params");
			if (params != NULL) {
				{
					json_t *const obj = json_object_get(params, "audio");
					if (obj != NULL && json_is_boolean(obj)) {
						with_acap = (_g_rtpa != NULL && json_boolean_value(obj));
					}
				}
				{
					json_t *const obj = json_object_get(params, "mic");
					if (obj != NULL && json_is_boolean(obj)) {
						with_aplay = (_g_config->aplay_dev_name != NULL && with_acap && json_boolean_value(obj));
					}
				}
				{
					json_t *const obj = json_object_get(params, "orientation");
					if (obj != NULL && json_is_integer(obj)) {
						video_orient = json_integer_value(obj);
						switch (video_orient) {
							case 90: case 180: case 270: break;
							default: video_orient = 0; break;
						}
					}
				}
			}
		}

		{
			char *sdp;
			char *const video_sdp = us_rtpv_make_sdp(_g_rtpv);
			char *const audio_sdp = (with_acap ? us_rtpa_make_sdp(_g_rtpa, with_aplay) : us_strdup(""));
			US_ASPRINTF(sdp,
				"v=0" RN
				"o=- %" PRIu64 " 1 IN IP4 0.0.0.0" RN
				"s=PiKVM uStreamer" RN
				"t=0 0" RN
				"%s%s",
				us_get_now_id() >> 1,
#				if JANUS_PLUGIN_API_VERSION >= 100
				// Place video SDP before audio SDP so that the video and audio streams
				// have predictable indices, even if audio is not available.
				// See also client.c.
				video_sdp, audio_sdp
#				else
				// For versions of Janus prior to 1.x, place the audio SDP first.
				audio_sdp, video_sdp
#				endif
			);
			json_t *const offer_jsep = json_pack("{ssss}", "type", "offer", "sdp", sdp);
			PUSH_STATUS("started", NULL, offer_jsep);
			json_decref(offer_jsep);
			free(audio_sdp);
			free(video_sdp);
			free(sdp);
		}

		{
			_LOCK_ALL;
			bool has_listeners = false;
			bool has_speakers = false;
			US_LIST_ITERATE(_g_clients, client, {
				if (client->session == session) {
					atomic_store(&client->transmit_acap, with_acap);
					atomic_store(&client->transmit_aplay, with_aplay);
					atomic_store(&client->video_orient, video_orient);
				}
				has_listeners = (has_listeners || atomic_load(&client->transmit_acap));
				has_speakers = (has_speakers || atomic_load(&client->transmit_aplay));
			});
			atomic_store(&_g_has_listeners, has_listeners);
			atomic_store(&_g_has_speakers, has_speakers);
			_UNLOCK_ALL;
		}

	} else if (!strcmp(request_str, "features")) {
		json_t *const features = json_pack(
			"{sbsb}",
			"audio", (_g_rtpa != NULL),
			"mic", (_g_rtpa != NULL && _g_config->aplay_dev_name != NULL)
		);
		PUSH_STATUS("features", features, NULL);
		json_decref(features);

	} else if (!strcmp(request_str, "key_required")) {
		// US_JLOG_INFO("main", "Got key_required message");
		atomic_store(&_g_key_required, true);

	} else {
		PUSH_ERROR(405, "Not implemented");
	}

done:
	US_DELETE(transaction, free);
	US_DELETE(msg, json_decref);
	US_DELETE(jsep, json_decref);

	return janus_plugin_result_new(
		result_type, result_msg,
		(result_type == JANUS_PLUGIN_OK ? json_pack("{sb}", "ok", 1) : NULL));

#	undef PUSH_STATUS
#	undef PUSH_ERROR
}

static void _plugin_incoming_rtp(janus_plugin_session *session, janus_plugin_rtp *packet) {
	_IF_DISABLED({ return; });
	if (session == NULL || packet == NULL || packet->video) {
		return; // Accept only valid audio
	}
	_LOCK_APLAY;
	US_LIST_ITERATE(_g_clients, client, {
		if (client->session == session) {
			us_janus_client_recv(client, packet);
			break;
		}
	});
	_UNLOCK_APLAY;
}

static void _plugin_incoming_rtcp(janus_plugin_session *session, janus_plugin_rtcp *packet) {
	_IF_DISABLED({ return; });
	if (session == NULL || packet == NULL || !packet->video) {
		return; // Accept only valid video
	}
	if (janus_rtcp_has_pli(packet->buffer, packet->length)) {
		// US_JLOG_INFO("main", "Got video PLI");
		atomic_store(&_g_key_required, true);
	}
}


// ***** Plugin *****

static int _plugin_get_api_compatibility(void)		{ return JANUS_PLUGIN_API_VERSION; }
static int _plugin_get_version(void)				{ return US_VERSION_U; }
static const char *_plugin_get_version_string(void)	{ return US_VERSION; }
static const char *_plugin_get_description(void)	{ return "PiKVM uStreamer Janus plugin for H.264 video"; }
static const char *_plugin_get_name(void)			{ return US_PLUGIN_NAME; }
static const char *_plugin_get_author(void)			{ return "Maxim Devaev <mdevaev@gmail.com>"; }
static const char *_plugin_get_package(void)		{ return US_PLUGIN_PACKAGE; }

janus_plugin *create(void) {
#	pragma GCC diagnostic push
#	pragma GCC diagnostic ignored "-Woverride-init"
	static janus_plugin plugin = JANUS_PLUGIN_INIT(
		.init = _plugin_init,
		.destroy = _plugin_destroy,

		.create_session = _plugin_create_session,
		.destroy_session = _plugin_destroy_session,
		.query_session = _plugin_query_session,

		.setup_media = _plugin_setup_media,
		.hangup_media = _plugin_hangup_media,

		.handle_message = _plugin_handle_message,

		.get_api_compatibility = _plugin_get_api_compatibility,
		.get_version = _plugin_get_version,
		.get_version_string = _plugin_get_version_string,
		.get_description = _plugin_get_description,
		.get_name = _plugin_get_name,
		.get_author = _plugin_get_author,
		.get_package = _plugin_get_package,

		.incoming_rtp = _plugin_incoming_rtp,
		.incoming_rtcp = _plugin_incoming_rtcp,
	);
#	pragma GCC diagnostic pop
	return &plugin;
}
