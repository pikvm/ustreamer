/*****************************************************************************
#                                                                            #
#    uStreamer - Lightweight and fast MJPEG-HTTP streamer.                   #
#                                                                            #
#    Copyright (C) 2018-2022  Maxim Devaev <mdevaev@gmail.com>               #
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
#include <janus/config.h>
#include <janus/plugins/plugin.h>

#include "uslibs/config.h"
#include "uslibs/tools.h"
#include "uslibs/threading.h"
#include "uslibs/list.h"
#include "uslibs/memsinksh.h"

#include "config.h"
#include "jlogging.h"
#include "audio.h"
#include "tc358743.h"
#include "rtpv.h"
#include "rtpa.h"
#include "memsinkfd.h"


static int _plugin_init(janus_callbacks *gw, const char *config_file_path);
static void _plugin_destroy(void);

static void _plugin_create_session(janus_plugin_session *session, int *err);
static void _plugin_destroy_session(janus_plugin_session *session, int *err);
static json_t *_plugin_query_session(janus_plugin_session *session);

static void _plugin_setup_media(janus_plugin_session *session);
static void _plugin_hangup_media(janus_plugin_session *session);

static struct janus_plugin_result *_plugin_handle_message(
	janus_plugin_session *session, char *transaction, json_t *msg, json_t *jsep);


static int _plugin_get_api_compatibility(void)		{ return JANUS_PLUGIN_API_VERSION; }
static int _plugin_get_version(void)				{ return VERSION_U; }
static const char *_plugin_get_version_string(void)	{ return VERSION; }
static const char *_plugin_get_description(void)	{ return "PiKVM uStreamer Janus plugin for H.264 video"; }
static const char *_plugin_get_name(void)			{ return PLUGIN_NAME; }
static const char *_plugin_get_author(void)			{ return "Maxim Devaev <mdevaev@gmail.com>"; }
static const char *_plugin_get_package(void)		{ return PLUGIN_PACKAGE; }

static void _plugin_incoming_rtp(UNUSED janus_plugin_session *handle, UNUSED janus_plugin_rtp *packet) {
	// Just a stub to avoid logging spam about the plugin's purpose
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Woverride-init"
static janus_plugin _plugin = JANUS_PLUGIN_INIT(
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
);
#pragma GCC diagnostic pop

janus_plugin *create(void) { // cppcheck-suppress unusedFunction
	return &_plugin;
}


typedef struct _client_sx {
	janus_plugin_session *session;
	bool transmit;

	LIST_STRUCT(struct _client_sx);
} _client_s;


static char				*_g_video_sink_name = NULL;
static char				*_g_audio_dev_name = NULL;
static char				*_g_tc358743_dev_path = NULL;

const useconds_t		_g_watchers_polling = 100000;

static _client_s		*_g_clients = NULL;
static janus_callbacks	*_g_gw = NULL;
static rtpv_s			*_g_rtpv = NULL;
static rtpa_s			*_g_rtpa = NULL;

static pthread_t		_g_video_tid;
static atomic_bool		_g_video_tid_created = false;
static pthread_t		_g_audio_tid;
static atomic_bool		_g_audio_tid_created = false;

static pthread_mutex_t	_g_lock = PTHREAD_MUTEX_INITIALIZER;
static atomic_bool		_g_ready = false;
static atomic_bool		_g_stop = false;
static atomic_bool		_g_has_watchers = false;


#define LOCK			A_MUTEX_LOCK(&_g_lock)
#define UNLOCK			A_MUTEX_UNLOCK(&_g_lock)
#define READY			atomic_load(&_g_ready)
#define STOP			atomic_load(&_g_stop)
#define HAS_WATCHERS	atomic_load(&_g_has_watchers)


static void _relay_rtp_clients(const rtp_s *rtp) {
	janus_plugin_rtp packet = {0};
	packet.video = rtp->video;
	packet.buffer = (char *)rtp->datagram;
	packet.length = rtp->used;
	janus_plugin_rtp_extensions_reset(&packet.extensions);
	LIST_ITERATE(_g_clients, client, {
		if (client->transmit) {
			_g_gw->relay_rtp(client->session, &packet);
		}
	});
}

#define IF_NOT_REPORTED(...) { \
		unsigned _error_code = __LINE__; \
		if (error_reported != _error_code) { __VA_ARGS__; error_reported = _error_code; } \
	}

static void *_clients_video_thread(UNUSED void *arg) {
	A_THREAD_RENAME("us_v_clients");
	atomic_store(&_g_video_tid_created, true);
	atomic_store(&_g_ready, true);

	frame_s *frame = frame_init();
	uint64_t frame_id = 0;

	unsigned error_reported = 0;

	while (!STOP) {
		if (!HAS_WATCHERS) {
			IF_NOT_REPORTED({ JLOG_INFO("video", "No active watchers, memsink disconnected"); });
			usleep(_g_watchers_polling);
			continue;
		}

		int fd = -1;
		memsink_shared_s *mem = NULL;

		if ((fd = shm_open(_g_video_sink_name, O_RDWR, 0)) <= 0) {
			IF_NOT_REPORTED({ JLOG_PERROR("video", "Can't open memsink"); });
			goto close_memsink;
		}

		if ((mem = memsink_shared_map(fd)) == NULL) {
			IF_NOT_REPORTED({ JLOG_PERROR("video", "Can't map memsink"); });
			goto close_memsink;
		}

		error_reported = 0;

		JLOG_INFO("video", "Memsink opened; reading frames ...");
		while (!STOP && HAS_WATCHERS) {
			int result = memsink_fd_wait_frame(fd, mem, frame_id);
			if (result == 0) {
				if (memsink_fd_get_frame(fd, mem, frame, &frame_id) != 0) {
					goto close_memsink;
				}
				LOCK;
				rtpv_wrap(_g_rtpv, frame);
				UNLOCK;
			} else if (result == -1) {
				goto close_memsink;
			}
		}

		close_memsink:
			if (mem != NULL) {
				JLOG_INFO("video", "Memsink closed");
				memsink_shared_unmap(mem);
				mem = NULL;
			}
			if (fd > 0) {
				close(fd);
				fd = -1;
			}
			sleep(1); // error_delay
	}

	frame_destroy(frame);
	return NULL;
}

static void *_clients_audio_thread(UNUSED void *arg) {
	A_THREAD_RENAME("us_a_clients");
	atomic_store(&_g_audio_tid_created, true);
	assert(_g_audio_dev_name);
	assert(_g_tc358743_dev_path);

	unsigned error_reported = 0;

	while (!STOP) {
		if (!HAS_WATCHERS) {
			usleep(_g_watchers_polling);
			continue;
		}

		tc358743_info_s info = {0};
		audio_s *audio = NULL;

		if (tc358743_read_info(_g_tc358743_dev_path, &info) < 0) {
			goto close_audio;
		}
		if (!info.has_audio) {
			IF_NOT_REPORTED({ JLOG_INFO("audio", "No audio presented from the host"); });
			goto close_audio;
		}
		IF_NOT_REPORTED({ JLOG_INFO("audio", "Detected host audio"); });
		if ((audio = audio_init(_g_audio_dev_name, info.audio_hz)) == NULL) {
			goto close_audio;
		}

		error_reported = 0;

		while (!STOP && HAS_WATCHERS) {
			if (
				tc358743_read_info(_g_tc358743_dev_path, &info) < 0
				|| !info.has_audio
				|| audio->pcm_hz != info.audio_hz
			) {
				goto close_audio;
			}

			size_t size = RTP_DATAGRAM_SIZE - RTP_HEADER_SIZE;
			uint8_t data[size];
			uint64_t pts;
			int result = audio_get_encoded(audio, data, &size, &pts);
			if (result == 0) {
				LOCK;
				rtpa_wrap(_g_rtpa, data, size, pts);
				UNLOCK;
			} else if (result == -1) {
				goto close_audio;
			}
		}

		close_audio:
			if (audio != NULL) {
				audio_destroy(audio);
			}
			sleep(1); // error_delay
	}
	return NULL;
}

#undef IF_NOT_REPORTED

static char *_get_config_value(janus_config *config, const char *section, const char *option) {
	janus_config_category *section_obj = janus_config_get_create(config, NULL, janus_config_type_category, section);
	janus_config_item *option_obj = janus_config_get(config, section_obj, janus_config_type_item, option);
	if (option_obj == NULL || option_obj->value == NULL || option_obj->value[0] == '\0') {
		return NULL;
	}
	return strdup(option_obj->value);
}

static int _read_config(const char *config_dir_path) {
	int retval = 0;

	char *config_file_path;
	janus_config *config = NULL;

	A_ASPRINTF(config_file_path, "%s/%s.jcfg", config_dir_path, PLUGIN_PACKAGE);
	JLOG_INFO("main", "Reading config file '%s' ...", config_file_path);

	config = janus_config_parse(config_file_path);
	if (config == NULL) {
		JLOG_ERROR("main", "Can't read config");
		goto error;
	}
	janus_config_print(config);

	if (
		(_g_video_sink_name = _get_config_value(config, "memsink", "object")) == NULL
		&& (_g_video_sink_name = _get_config_value(config, "video", "sink")) == NULL
	) {
		JLOG_ERROR("main", "Missing config value: video.sink (ex. memsink.object)");
		goto error;
	}
	if ((_g_audio_dev_name = _get_config_value(config, "audio", "device")) != NULL) {
		JLOG_INFO("main", "Enabled the experimental AUDIO feature");
		if ((_g_tc358743_dev_path = _get_config_value(config, "audio", "tc358743")) == NULL) {
			JLOG_INFO("main", "Missing config value: audio.tc358743");
			goto error;
		}
	}

	goto ok;
	error:
		retval = -1;
	ok:
		if (config) {
			janus_config_destroy(config);
		}
		free(config_file_path);
		return retval;
}

static int _plugin_init(janus_callbacks *gw, const char *config_dir_path) {
	// https://groups.google.com/g/meetecho-janus/c/xoWIQfaoJm8
	// sysctl -w net.core.rmem_default=500000 
	// sysctl -w net.core.wmem_default=500000 
	// sysctl -w net.core.rmem_max=1000000 
	// sysctl -w net.core.wmem_max=1000000

	JLOG_INFO("main", "Initializing plugin ...");
	assert(!atomic_load(&_g_video_tid_created));
	assert(!atomic_load(&_g_audio_tid_created));
	assert(!READY);
	assert(!STOP);
	if (gw == NULL || config_dir_path == NULL || _read_config(config_dir_path) < 0) {
		return -1;
	}
	_g_gw = gw;
	_g_rtpv = rtpv_init(_relay_rtp_clients);
	if (_g_audio_dev_name) {
		_g_rtpa = rtpa_init(_relay_rtp_clients);
		A_THREAD_CREATE(&_g_audio_tid, _clients_audio_thread, NULL);
	}
	A_THREAD_CREATE(&_g_video_tid, _clients_video_thread, NULL);
	return 0;
}

static void _plugin_destroy(void) {
	JLOG_INFO("main", "Destroying plugin ...");
	atomic_store(&_g_stop, true);
	if (atomic_load(&_g_video_tid_created)) {
		A_THREAD_JOIN(_g_video_tid);
	}
	if (atomic_load(&_g_audio_tid_created)) {
		A_THREAD_JOIN(_g_audio_tid);
	}

	LIST_ITERATE(_g_clients, client, {
		LIST_REMOVE(_g_clients, client);
		free(client);
	});
	_g_clients = NULL;

#	define DEL(_func, _var) { if (_var) { _func(_var); _var = NULL; } }
	DEL(rtpa_destroy, _g_rtpa);
	DEL(rtpv_destroy, _g_rtpv);
	_g_gw = NULL;
	DEL(free, _g_tc358743_dev_path);
	DEL(free, _g_audio_dev_name);
	DEL(free, _g_video_sink_name);
#	undef DEL
}

#define IF_DISABLED(...) { if (!READY || STOP) { __VA_ARGS__ } }

static void _plugin_create_session(janus_plugin_session *session, int *err) {
	IF_DISABLED({ *err = -1; return; });
	LOCK;
	JLOG_INFO("main", "Creating session %p ...", session);
	_client_s *client;
	A_CALLOC(client, 1);
	client->session = session;
	client->transmit = true;
	LIST_APPEND(_g_clients, client);
	atomic_store(&_g_has_watchers, true);
	UNLOCK;
}

static void _plugin_destroy_session(janus_plugin_session* session, int *err) {
	IF_DISABLED({ *err = -1; return; });
	LOCK;
	bool found = false;
	bool has_watchers = false;
	LIST_ITERATE(_g_clients, client, {
		if (client->session == session) {
			JLOG_INFO("main", "Removing session %p ...", session);
			LIST_REMOVE(_g_clients, client);
			free(client);
			found = true;
		} else {
			has_watchers = (has_watchers || client->transmit);
		}
	});
	if (!found) {
		JLOG_WARN("main", "No session %p", session);
		*err = -2;
	}
	atomic_store(&_g_has_watchers, has_watchers);
	UNLOCK;
}

static json_t *_plugin_query_session(janus_plugin_session *session) {
	IF_DISABLED({ return NULL; });
	json_t *info = NULL;
	LOCK;
	LIST_ITERATE(_g_clients, client, {
		if (client->session == session) {
			info = json_string("session_found");
			break;
		}
	});
	UNLOCK;
	return info;
}

static void _set_transmit(janus_plugin_session *session, UNUSED const char *msg, bool transmit) {
	IF_DISABLED({ return; });
	LOCK;
	bool found = false;
	bool has_watchers = false;
	LIST_ITERATE(_g_clients, client, {
		if (client->session == session) {
			client->transmit = transmit;
			// JLOG_INFO("main", "%s session %p", msg, session);
			found = true;
		}
		has_watchers = (has_watchers || client->transmit);
	});
	if (!found) {
		JLOG_WARN("main", "No session %p", session);
	}
	atomic_store(&_g_has_watchers, has_watchers);
	UNLOCK;
}

#undef IF_DISABLED

static void _plugin_setup_media(janus_plugin_session *session) { _set_transmit(session, "Unmuted", true); }
static void _plugin_hangup_media(janus_plugin_session *session) { _set_transmit(session, "Muted", false); }

static struct janus_plugin_result *_plugin_handle_message(
	janus_plugin_session *session, char *transaction, json_t *msg, json_t *jsep) {

	assert(transaction != NULL);

#	define FREE_MSG_JSEP { \
			if (msg) json_decref(msg); \
			if (jsep) json_decref(jsep); \
		}

	if (session == NULL || msg == NULL) {
		free(transaction);
		FREE_MSG_JSEP;
		return janus_plugin_result_new(JANUS_PLUGIN_ERROR, (msg ? "No session" : "No message"), NULL);
	}

#	define PUSH_ERROR(_error, _reason) { \
			/*JLOG_ERROR("main", "Message error in session %p: %s", session, _reason);*/ \
			json_t *_event = json_object(); \
			json_object_set_new(_event, "ustreamer", json_string("event")); \
			json_object_set_new(_event, "error_code", json_integer(_error)); \
			json_object_set_new(_event, "error", json_string(_reason)); \
			_g_gw->push_event(session, &_plugin, transaction, _event, NULL); \
			json_decref(_event); \
		}

	json_t *request_obj = json_object_get(msg, "request");
	if (request_obj == NULL) {
		PUSH_ERROR(400, "Request missing");
		goto ok_wait;
	}

	const char *request_str = json_string_value(request_obj);
	if (!request_str) {
		PUSH_ERROR(400, "Request not a string");
		goto ok_wait;
	}
	// JLOG_INFO("main", "Message: %s", request_str);

#	define PUSH_STATUS(_status, _jsep) { \
			json_t *_event = json_object(); \
			json_object_set_new(_event, "ustreamer", json_string("event")); \
			json_t *_result = json_object(); \
			json_object_set_new(_result, "status", json_string(_status)); \
			json_object_set_new(_event, "result", _result); \
			_g_gw->push_event(session, &_plugin, transaction, _event, _jsep); \
			json_decref(_event); \
		}

	if (!strcmp(request_str, "start")) {
		PUSH_STATUS("started", NULL);

	} else if (!strcmp(request_str, "stop")) {
		PUSH_STATUS("stopped", NULL);

	} else if (!strcmp(request_str, "watch")) {
		char *sdp;
		{
			char *video_sdp = rtpv_make_sdp(_g_rtpv);
			if (video_sdp == NULL) {
				PUSH_ERROR(503, "Haven't received SPS/PPS from memsink yet");
				goto ok_wait;
			}
			char *audio_sdp = (_g_rtpa ? rtpa_make_sdp(_g_rtpa) : strdup(""));
			A_ASPRINTF(sdp,
				"v=0" RN
				"o=- %" PRIu64 " 1 IN IP4 0.0.0.0" RN
				"s=PiKVM uStreamer" RN
				"t=0 0" RN
				"%s%s",
				get_now_id() >> 1, audio_sdp, video_sdp
			);
			free(audio_sdp);
			free(video_sdp);
		}
		json_t *offer_jsep = json_pack("{ssss}", "type", "offer", "sdp", sdp);
		free(sdp);
		PUSH_STATUS("started", offer_jsep);
		json_decref(offer_jsep);

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
