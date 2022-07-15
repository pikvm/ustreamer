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


#include "client.h"


static void *_video_thread(void *v_client);
static void *_audio_thread(void *v_client);
static void *_common_thread(void *v_client, bool video);


client_s *client_init(janus_callbacks *gw, janus_plugin_session *session, bool has_audio) {
	client_s *client;
	A_CALLOC(client, 1);
	client->gw = gw;
	client->session = session;
	atomic_init(&client->transmit, true);

	atomic_init(&client->stop, false);

	client->video_queue = queue_init(1024);
	A_THREAD_CREATE(&client->video_tid, _video_thread, client);

	if (has_audio) {
		client->audio_queue = queue_init(64);
		A_THREAD_CREATE(&client->audio_tid, _audio_thread, client);
	}
	return client;
}

void client_destroy(client_s *client) {
	atomic_store(&client->stop, true);
	queue_put(client->video_queue, NULL, 0);
	if (client->audio_queue != NULL) {
		queue_put(client->audio_queue, NULL, 0);
	}

	A_THREAD_JOIN(client->video_tid);
	QUEUE_FREE_ITEMS_AND_DESTROY(client->video_queue, rtp_destroy);
	if (client->audio_queue != NULL) {
		A_THREAD_JOIN(client->audio_tid);
		QUEUE_FREE_ITEMS_AND_DESTROY(client->audio_queue, rtp_destroy);
	}
	free(client);
}

void client_send(client_s *client, const rtp_s *rtp) {
	if (
		!atomic_load(&client->transmit)
		|| (!rtp->video && client->audio_queue == NULL)
	) {
		return;
	}
	rtp_s *new = rtp_dup(rtp);
	if (queue_put((new->video ? client->video_queue : client->audio_queue), new, 0) != 0) {
		JLOG_ERROR("client", "Session %p %s queue is full",
			client->session, (new->video ? "video" : "audio"));
		rtp_destroy(new);
	}
}

static void *_video_thread(void *v_client) {
	return _common_thread(v_client, true);
}

static void *_audio_thread(void *v_client) {
	return _common_thread(v_client, false);
}

static void *_common_thread(void *v_client, bool video) {
	client_s *client = (client_s *)v_client;
	queue_s *queue = (video ? client->video_queue : client->audio_queue);
	assert(queue != NULL); // Audio may be NULL

	while (!atomic_load(&client->stop)) {
		rtp_s *rtp;
		if (!queue_get(queue, (void **)&rtp, 0.1)) {
			if (rtp == NULL) {
				break;
			}
			if (atomic_load(&client->transmit)) {
				janus_plugin_rtp packet = {0};
				packet.video = rtp->video;
				packet.buffer = (char *)rtp->datagram;
				packet.length = rtp->used;
				janus_plugin_rtp_extensions_reset(&packet.extensions);
				if (video) {
					packet.extensions.min_delay = 0;
					packet.extensions.max_delay = 0;
				}
				client->gw->relay_rtp(client->session, &packet);
			}
			rtp_destroy(rtp);
		}
	}
	return NULL;
}
