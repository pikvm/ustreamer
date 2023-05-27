/*****************************************************************************
#                                                                            #
#    uStreamer - Lightweight and fast MJPEG-HTTP streamer.                   #
#                                                                            #
#    Copyright (C) 2018-2023  Maxim Devaev <mdevaev@gmail.com>               #
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


#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <inttypes.h>
#include <unistd.h>
#include <fcntl.h>
#include <assert.h>

#include <sys/mman.h>
#include <sys/stat.h>

#include <pthread.h>
#include <jansson.h>
#include <janus/plugins/plugin.h>
#include <janus/rtcp.h>

#include "uslibs/const.h"
#include "uslibs/tools.h"
#include "uslibs/threading.h"
#include "uslibs/list.h"
#include "uslibs/memsinksh.h"

#include "const.h"
#include "logging.h"
#include "queue.h"
#include "client.h"
#include "audio.h"
#include "tc358743.h"
#include "rtp.h"
#include "rtpv.h"
#include "rtpa.h"
#include "memsinkfd.h"
#include "config.h"


static us_config_s	*_g_config = NULL;
const useconds_t	_g_watchers_polling = 100000;

static us_janus_client_s	*_g_clients = NULL;
static janus_callbacks		*_g_gw = NULL;
static us_queue_s			*_g_video_queue = NULL;
static us_rtpv_s			*_g_rtpv = NULL;
static us_rtpa_s			*_g_rtpa = NULL;

static pthread_t		_g_video_rtp_tid;
static atomic_bool		_g_video_rtp_tid_created = false;
static pthread_t		_g_video_sink_tid;
static atomic_bool		_g_video_sink_tid_created = false;
static pthread_t		_g_audio_tid;
static atomic_bool		_g_audio_tid_created = false;

static pthread_mutex_t	_g_video_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t	_g_audio_lock = PTHREAD_MUTEX_INITIALIZER;
static atomic_bool		_g_ready = false;
static atomic_bool		_g_stop = false;
static atomic_bool		_g_has_watchers = false;
static atomic_bool		_g_has_listeners = false;
static atomic_bool		_g_key_required = false;


#define _LOCK_VIDEO		US_MUTEX_LOCK(_g_video_lock)
#define _UNLOCK_VIDEO	US_MUTEX_UNLOCK(_g_video_lock)

#define _LOCK_AUDIO		US_MUTEX_LOCK(_g_audio_lock)
#define _UNLOCK_AUDIO	US_MUTEX_UNLOCK(_g_audio_lock)

#define _LOCK_ALL		{ _LOCK_VIDEO; _LOCK_AUDIO; }
#define _UNLOCK_ALL		{ _UNLOCK_AUDIO; _UNLOCK_VIDEO; }

#define _READY			atomic_load(&_g_ready)
#define _STOP			atomic_load(&_g_stop)
#define _HAS_WATCHERS	atomic_load(&_g_has_watchers)
#define _HAS_LISTENERS	atomic_load(&_g_has_listeners)


janus_plugin *create(void);


static void *_video_rtp_thread(UNUSED void *arg) {
	US_THREAD_RENAME("us_video_rtp");
	atomic_store(&_g_video_rtp_tid_created, true);

	while (!_STOP) {
		us_frame_s *frame;
		if (us_queue_get(_g_video_queue, (void **)&frame, 0.1) == 0) {
			_LOCK_VIDEO;
			us_rtpv_wrap(_g_rtpv, frame);
			_UNLOCK_VIDEO;
			us_frame_destroy(frame);
		}
	}
	return NULL;
}

static void *_video_sink_thread(UNUSED void *arg) {
	US_THREAD_RENAME("us_video_sink");
	atomic_store(&_g_video_sink_tid_created, true);

	uint64_t frame_id = 0;
	int once = 0;

	while (!_STOP) {
		if (!_HAS_WATCHERS) {
			US_ONCE({ US_JLOG_INFO("video", "No active watchers, memsink disconnected"); });
			usleep(_g_watchers_polling);
			continue;
		}

		int fd = -1;
		us_memsink_shared_s *mem = NULL;

		if ((fd = shm_open(_g_config->video_sink_name, O_RDWR, 0)) <= 0) {
			US_ONCE({ US_JLOG_PERROR("video", "Can't open memsink"); });
			goto close_memsink;
		}

		if ((mem = us_memsink_shared_map(fd)) == NULL) {
			US_ONCE({ US_JLOG_PERROR("video", "Can't map memsink"); });
			goto close_memsink;
		}

		once = 0;

		US_JLOG_INFO("video", "Memsink opened; reading frames ...");
		while (!_STOP && _HAS_WATCHERS) {
			const int result = us_memsink_fd_wait_frame(fd, mem, frame_id);
			if (result == 0) {
				us_frame_s *const frame = us_memsink_fd_get_frame(fd, mem, &frame_id, atomic_load(&_g_key_required));
				if (frame == NULL) {
					goto close_memsink;
				}
				if (frame->key) {
					atomic_store(&_g_key_required, false);
				}
				if (us_queue_put(_g_video_queue, frame, 0) != 0) {
					US_ONCE({ US_JLOG_PERROR("video", "Video queue is full"); });
					us_frame_destroy(frame);
				}
			} else if (result == -1) {
				goto close_memsink;
			}
		}

		close_memsink:
			if (mem != NULL) {
				US_JLOG_INFO("video", "Memsink closed");
				us_memsink_shared_unmap(mem);
			}
			if (fd >= 0) {
				close(fd);
			}
			sleep(1); // error_delay
	}
	return NULL;
}

static void *_audio_thread(UNUSED void *arg) {
	US_THREAD_RENAME("us_audio");
	atomic_store(&_g_audio_tid_created, true);
	assert(_g_config->audio_dev_name != NULL);
	assert(_g_config->tc358743_dev_path != NULL);

	int once = 0;

	while (!_STOP) {
		if (!_HAS_WATCHERS || !_HAS_LISTENERS) {
			usleep(_g_watchers_polling);
			continue;
		}

		us_tc358743_info_s info = {0};
		us_audio_s *audio = NULL;

		if (us_tc358743_read_info(_g_config->tc358743_dev_path, &info) < 0) {
			goto close_audio;
		}
		if (!info.has_audio) {
			US_ONCE({ US_JLOG_INFO("audio", "No audio presented from the host"); });
			goto close_audio;
		}
		US_ONCE({ US_JLOG_INFO("audio", "Detected host audio"); });
		if ((audio = us_audio_init(_g_config->audio_dev_name, info.audio_hz)) == NULL) {
			goto close_audio;
		}

		once = 0;

		while (!_STOP && _HAS_WATCHERS && _HAS_LISTENERS) {
			if (
				us_tc358743_read_info(_g_config->tc358743_dev_path, &info) < 0
				|| !info.has_audio
				|| audio->pcm_hz != info.audio_hz
			) {
				goto close_audio;
			}

			size_t size = US_RTP_DATAGRAM_SIZE - US_RTP_HEADER_SIZE;
			uint8_t data[size];
			uint64_t pts;
			const int result = us_audio_get_encoded(audio, data, &size, &pts);
			if (result == 0) {
				_LOCK_AUDIO;
				us_rtpa_wrap(_g_rtpa, data, size, pts);
				_UNLOCK_AUDIO;
			} else if (result == -1) {
				goto close_audio;
			}
		}

		close_audio:
			US_DELETE(audio, us_audio_destroy);
			sleep(1); // error_delay
	}
	return NULL;
}

static void _relay_rtp_clients(const us_rtp_s *rtp) {
	US_LIST_ITERATE(_g_clients, client, {
		us_janus_client_send(client, rtp);
	});
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

	_g_video_queue = us_queue_init(1024);
	_g_rtpv = us_rtpv_init(_relay_rtp_clients);
	if (_g_config->audio_dev_name != NULL && us_audio_probe(_g_config->audio_dev_name)) {
		_g_rtpa = us_rtpa_init(_relay_rtp_clients);
		US_THREAD_CREATE(_g_audio_tid, _audio_thread, NULL);
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
	JOIN(_g_audio_tid);
#	undef JOIN

	US_LIST_ITERATE(_g_clients, client, {
		US_LIST_REMOVE(_g_clients, client);
		us_janus_client_destroy(client);
	});

	US_QUEUE_DELETE_WITH_ITEMS(_g_video_queue, us_frame_destroy);

	US_DELETE(_g_rtpa, us_rtpa_destroy);
	US_DELETE(_g_rtpv, us_rtpv_destroy);
	US_DELETE(_g_config, us_config_destroy);
}

#define _IF_DISABLED(...) { if (!_READY || _STOP) { __VA_ARGS__ } }

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
	US_LIST_ITERATE(_g_clients, client, {
		if (client->session == session) {
			US_JLOG_INFO("main", "Removing session %p ...", session);
			US_LIST_REMOVE(_g_clients, client);
			us_janus_client_destroy(client);
			found = true;
		} else {
			has_watchers = (has_watchers || atomic_load(&client->transmit));
			has_listeners = (has_listeners || atomic_load(&client->transmit_audio));
		}
	});
	if (!found) {
		US_JLOG_WARN("main", "No session %p", session);
		*err = -2;
	}
	atomic_store(&_g_has_watchers, has_watchers);
	atomic_store(&_g_has_listeners, has_listeners);
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

static void _set_transmit(janus_plugin_session *session, UNUSED const char *msg, bool transmit) {
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

#undef _IF_DISABLED

static void _plugin_setup_media(janus_plugin_session *session) { _set_transmit(session, "Unmuted", true); }
static void _plugin_hangup_media(janus_plugin_session *session) { _set_transmit(session, "Muted", false); }

static struct janus_plugin_result *_plugin_handle_message(
	janus_plugin_session *session, char *transaction, json_t *msg, json_t *jsep) {

	assert(transaction != NULL);

#	define FREE_MSG_JSEP { \
			US_DELETE(msg, json_decref); \
			US_DELETE(jsep, json_decref); \
		}

	if (session == NULL || msg == NULL) {
		free(transaction);
		FREE_MSG_JSEP;
		return janus_plugin_result_new(JANUS_PLUGIN_ERROR, (msg ? "No session" : "No message"), NULL);
	}

#	define PUSH_ERROR(x_error, x_reason) { \
			/*US_JLOG_ERROR("main", "Message error in session %p: %s", session, x_reason);*/ \
			json_t *m_event = json_object(); \
			json_object_set_new(m_event, "ustreamer", json_string("event")); \
			json_object_set_new(m_event, "error_code", json_integer(x_error)); \
			json_object_set_new(m_event, "error", json_string(x_reason)); \
			_g_gw->push_event(session, create(), transaction, m_event, NULL); \
			json_decref(m_event); \
		}

	json_t *const request = json_object_get(msg, "request");
	if (request == NULL) {
		PUSH_ERROR(400, "Request missing");
		goto ok_wait;
	}

	const char *const request_str = json_string_value(request);
	if (request_str == NULL) {
		PUSH_ERROR(400, "Request not a string");
		goto ok_wait;
	}
	// US_JLOG_INFO("main", "Message: %s", request_str);

#	define PUSH_STATUS(x_status, x_payload, x_jsep) { \
			json_t *const m_event = json_object(); \
			json_object_set_new(m_event, "ustreamer", json_string("event")); \
			json_t *const m_result = json_object(); \
			json_object_set_new(m_result, "status", json_string(x_status)); \
			if (x_payload != NULL) { \
				json_object_set_new(m_result, x_status, x_payload); \
			} \
			json_object_set_new(m_event, "result", m_result); \
			_g_gw->push_event(session, create(), transaction, m_event, x_jsep); \
			json_decref(m_event); \
		}

	if (!strcmp(request_str, "start")) {
		PUSH_STATUS("started", NULL, NULL);

	} else if (!strcmp(request_str, "stop")) {
		PUSH_STATUS("stopped", NULL, NULL);

	} else if (!strcmp(request_str, "watch")) {
		bool with_audio = false;
		{
			json_t *const params = json_object_get(msg, "params");
			if (params != NULL) {
				json_t *const audio = json_object_get(params, "audio");
				if (audio != NULL && json_is_boolean(audio)) {
					with_audio = (_g_rtpa != NULL && json_boolean_value(audio));
				}
			}
		}

		{
			char *sdp;
			char *const video_sdp = us_rtpv_make_sdp(_g_rtpv);
			char *const audio_sdp = (with_audio ? us_rtpa_make_sdp(_g_rtpa) : us_strdup(""));
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
			US_LIST_ITERATE(_g_clients, client, {
				if (client->session == session) {
					atomic_store(&client->transmit_audio, with_audio);
				}
				has_listeners = (has_listeners || atomic_load(&client->transmit_audio));
			});
			atomic_store(&_g_has_listeners, has_listeners);
			_UNLOCK_ALL;
		}

	} else if (!strcmp(request_str, "features")) {
		json_t *const features = json_pack("{sb}", "audio", (_g_rtpa != NULL));
		PUSH_STATUS("features", features, NULL);
		json_decref(features);

	} else if (!strcmp(request_str, "key_required")) {
		// US_JLOG_INFO("main", "Got key_required message");
		atomic_store(&_g_key_required, true);

	} else {
		PUSH_ERROR(405, "Not implemented");
	}

	ok_wait:
		FREE_MSG_JSEP;
		return janus_plugin_result_new(JANUS_PLUGIN_OK_WAIT, NULL, NULL);

#	undef PUSH_STATUS
#	undef PUSH_ERROR
#	undef FREE_MSG_JSEP
}

static void _plugin_incoming_rtcp(UNUSED janus_plugin_session *handle, UNUSED janus_plugin_rtcp *packet) {
	if (packet->video && janus_rtcp_has_pli(packet->buffer, packet->length)) {
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

		.incoming_rtcp = _plugin_incoming_rtcp,
	);
#	pragma GCC diagnostic pop
	return &plugin;
}
