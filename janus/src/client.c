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


#include "client.h"


static void *_video_thread(void *v_client);
static void *_audio_thread(void *v_client);
static void *_common_thread(void *v_client, bool video);


us_janus_client_s *us_janus_client_init(janus_callbacks *gw, janus_plugin_session *session) {
	us_janus_client_s *client;
	US_CALLOC(client, 1);
	client->gw = gw;
	client->session = session;
	atomic_init(&client->transmit, false);
	atomic_init(&client->transmit_audio, false);

	atomic_init(&client->stop, false);

	client->video_queue = us_queue_init(1024);
	US_THREAD_CREATE(client->video_tid, _video_thread, client);

	client->audio_queue = us_queue_init(64);
	US_THREAD_CREATE(client->audio_tid, _audio_thread, client);

	return client;
}

void us_janus_client_destroy(us_janus_client_s *client) {
	atomic_store(&client->stop, true);
	us_queue_put(client->video_queue, NULL, 0);
	us_queue_put(client->audio_queue, NULL, 0);

	US_THREAD_JOIN(client->video_tid);
	US_QUEUE_DELETE_WITH_ITEMS(client->video_queue, us_rtp_destroy);

	US_THREAD_JOIN(client->audio_tid);
	US_QUEUE_DELETE_WITH_ITEMS(client->audio_queue, us_rtp_destroy);

	free(client);
}

void us_janus_client_send(us_janus_client_s *client, const us_rtp_s *rtp) {
	if (
		atomic_load(&client->transmit)
		&& (rtp->video || atomic_load(&client->transmit_audio))
	) {
		us_rtp_s *const new = us_rtp_dup(rtp);
		if (us_queue_put((new->video ? client->video_queue : client->audio_queue), new, 0) != 0) {
			US_JLOG_ERROR("client", "Session %p %s queue is full",
				client->session, (new->video ? "video" : "audio"));
			us_rtp_destroy(new);
		}
	}
}

static void *_video_thread(void *v_client) {
	return _common_thread(v_client, true);
}

static void *_audio_thread(void *v_client) {
	return _common_thread(v_client, false);
}

static void *_common_thread(void *v_client, bool video) {
	us_janus_client_s *const client = (us_janus_client_s *)v_client;
	us_queue_s *const queue = (video ? client->video_queue : client->audio_queue);
	assert(queue != NULL); // Audio may be NULL

	while (!atomic_load(&client->stop)) {
		us_rtp_s *rtp;
		if (!us_queue_get(queue, (void **)&rtp, 0.1)) {
			if (rtp == NULL) {
				break;
			}

			if (
				atomic_load(&client->transmit)
				&& (video || atomic_load(&client->transmit_audio))
			) {
				janus_plugin_rtp packet = {0};
				packet.video = rtp->video;
				packet.buffer = (char *)rtp->datagram;
				packet.length = rtp->used;
#				if JANUS_PLUGIN_API_VERSION >= 100
				// The uStreamer Janus plugin places video in stream index 0 and audio
				// (if available) in stream index 1.
				packet.mindex = (rtp->video ? 0 : 1);
#				endif

				janus_plugin_rtp_extensions_reset(&packet.extensions);
				/*if (rtp->zero_playout_delay) {
					// https://github.com/pikvm/pikvm/issues/784
					packet.extensions.min_delay = 0;
					packet.extensions.max_delay = 0;
				} else {
					packet.extensions.min_delay = 0;
					packet.extensions.max_delay = 1000;
				}*/

				client->gw->relay_rtp(client->session, &packet);
			}
			us_rtp_destroy(rtp);
		}
	}
	return NULL;
}
