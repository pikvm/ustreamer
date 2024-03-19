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

#include <stdlib.h>
#include <stdatomic.h>
#include <string.h>

#include <pthread.h>
#include <janus/plugins/plugin.h>

#include "uslibs/types.h"
#include "uslibs/tools.h"
#include "uslibs/threading.h"
#include "uslibs/list.h"
#include "uslibs/ring.h"

#include "logging.h"
#include "rtp.h"


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
	atomic_init(&client->video_orient, 0);

	atomic_init(&client->stop, false);

	US_RING_INIT_WITH_ITEMS(client->video_ring, 2048, us_rtp_init);
	US_THREAD_CREATE(client->video_tid, _video_thread, client);

	US_RING_INIT_WITH_ITEMS(client->audio_ring, 64, us_rtp_init);
	US_THREAD_CREATE(client->audio_tid, _audio_thread, client);

	return client;
}

void us_janus_client_destroy(us_janus_client_s *client) {
	atomic_store(&client->stop, true);

	US_THREAD_JOIN(client->video_tid);
	US_RING_DELETE_WITH_ITEMS(client->video_ring, us_rtp_destroy);

	US_THREAD_JOIN(client->audio_tid);
	US_RING_DELETE_WITH_ITEMS(client->audio_ring, us_rtp_destroy);

	free(client);
}

void us_janus_client_send(us_janus_client_s *client, const us_rtp_s *rtp) {
	if (
		atomic_load(&client->transmit)
		&& (rtp->video || atomic_load(&client->transmit_audio))
	) {
		us_ring_s *const ring = (rtp->video ? client->video_ring : client->audio_ring);
		const int ri = us_ring_producer_acquire(ring, 0);
		if (ri < 0) {
			US_JLOG_ERROR("client", "Session %p %s ring is full",
				client->session, (rtp->video ? "video" : "audio"));
			return;
		}
		memcpy(ring->items[ri], rtp, sizeof(us_rtp_s));
		us_ring_producer_release(ring, ri);
	}
}

static void *_video_thread(void *v_client) {
	US_THREAD_SETTLE("us_c_video");
	return _common_thread(v_client, true);
}

static void *_audio_thread(void *v_client) {
	US_THREAD_SETTLE("us_c_audio");
	return _common_thread(v_client, false);
}

static void *_common_thread(void *v_client, bool video) {
	us_janus_client_s *const client = v_client;
	us_ring_s *const ring = (video ? client->video_ring : client->audio_ring);
	assert(ring != NULL); // Audio may be NULL

	while (!atomic_load(&client->stop)) {
		const int ri = us_ring_consumer_acquire(ring, 0.1);
		if (ri < 0) {
			continue;
		}
		us_rtp_s rtp;
		memcpy(&rtp, ring->items[ri], sizeof(us_rtp_s));
		us_ring_consumer_release(ring, ri);

		if (
			atomic_load(&client->transmit)
			&& (video || atomic_load(&client->transmit_audio))
		) {
			janus_plugin_rtp packet = {
				.video = rtp.video,
				.buffer = (char*)rtp.datagram,
				.length = rtp.used,
#				if JANUS_PLUGIN_API_VERSION >= 100
				// The uStreamer Janus plugin places video in stream index 0 and audio
				// (if available) in stream index 1.
				.mindex = (rtp.video ? 0 : 1),
#				endif
			};
			janus_plugin_rtp_extensions_reset(&packet.extensions);

			/*if (rtp->zero_playout_delay) {
				// https://github.com/pikvm/pikvm/issues/784
				packet.extensions.min_delay = 0;
				packet.extensions.max_delay = 0;
			} else {
				packet.extensions.min_delay = 0;
				// 10s - Chromium/WebRTC default
				// 3s - Firefox default
				packet.extensions.max_delay = 300; // == 3s, i.e. 10ms granularity
			}*/

			if (rtp.video) {
				const uint video_orient = atomic_load(&client->video_orient);
				if (video_orient != 0) {
					packet.extensions.video_rotation = video_orient;
				}
			}

			client->gw->relay_rtp(client->session, &packet);
		}
	}
	return NULL;
}
