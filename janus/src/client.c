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


#include "client.h"

#include <stdlib.h>
#include <stdatomic.h>
#include <string.h>
#include <assert.h>

#include <pthread.h>
#include <janus/plugins/plugin.h>
#include <janus/rtp.h>
#include <opus/opus.h>

#include "uslibs/types.h"
#include "uslibs/tools.h"
#include "uslibs/threading.h"
#include "uslibs/array.h"
#include "uslibs/list.h"
#include "uslibs/ring.h"

#include "logging.h"
#include "au.h"
#include "rtp.h"


static void *_video_thread(void *v_client);
static void *_acap_thread(void *v_client);
static void *_video_or_acap_thread(void *v_client, bool video);
static void *_aplay_thread(void *v_client);


us_janus_client_s *us_janus_client_init(janus_callbacks *gw, janus_plugin_session *session) {
	us_janus_client_s *client;
	US_CALLOC(client, 1);
	client->gw = gw;
	client->session = session;
	atomic_init(&client->transmit, false);
	atomic_init(&client->transmit_acap, false);
	atomic_init(&client->transmit_aplay, false);
	atomic_init(&client->video_orient, 0);

	atomic_init(&client->stop, false);

	US_RING_INIT_WITH_ITEMS(client->video_ring, 2048, us_rtp_init);
	US_THREAD_CREATE(client->video_tid, _video_thread, client);

	US_RING_INIT_WITH_ITEMS(client->acap_ring, 64, us_rtp_init);
	US_THREAD_CREATE(client->acap_tid, _acap_thread, client);

	US_RING_INIT_WITH_ITEMS(client->aplay_enc_ring, 64, us_au_encoded_init);
	US_RING_INIT_WITH_ITEMS(client->aplay_pcm_ring, 64, us_au_pcm_init);
	US_THREAD_CREATE(client->aplay_tid, _aplay_thread, client);

	return client;
}

void us_janus_client_destroy(us_janus_client_s *client) {
	atomic_store(&client->stop, true);

	US_THREAD_JOIN(client->video_tid);
	US_RING_DELETE_WITH_ITEMS(client->video_ring, us_rtp_destroy);

	US_THREAD_JOIN(client->acap_tid);
	US_RING_DELETE_WITH_ITEMS(client->acap_ring, us_rtp_destroy);

	US_THREAD_JOIN(client->aplay_tid);
	US_RING_DELETE_WITH_ITEMS(client->aplay_enc_ring, us_au_encoded_destroy);
	US_RING_DELETE_WITH_ITEMS(client->aplay_pcm_ring, us_au_pcm_destroy);

	free(client);
}

void us_janus_client_send(us_janus_client_s *client, const us_rtp_s *rtp) {
	if (
		atomic_load(&client->transmit)
		&& (rtp->video || atomic_load(&client->transmit_acap))
	) {
		us_ring_s *const ring = (rtp->video ? client->video_ring : client->acap_ring);
		const int ri = us_ring_producer_acquire(ring, 0);
		if (ri < 0) {
			US_JLOG_ERROR("client", "Session %p %s ring is full",
				client->session, (rtp->video ? "video" : "acap"));
			return;
		}
		memcpy(ring->items[ri], rtp, sizeof(us_rtp_s));
		us_ring_producer_release(ring, ri);
	}
}

void us_janus_client_recv(us_janus_client_s *client, janus_plugin_rtp *packet) {
	if (
		packet->video
		|| packet->length < sizeof(janus_rtp_header)
		|| !atomic_load(&client->transmit)
		|| !atomic_load(&client->transmit_aplay)
	) {
		return;
	}

	const janus_rtp_header *const header = (janus_rtp_header*)packet->buffer;
	if (header->type != US_RTP_OPUS_PAYLOAD) {
		return;
	}

	const u16 seq = ntohs(header->seq_number);
	if (
		seq >= client->aplay_seq_next // In order or missing
		|| (client->aplay_seq_next - seq) > 50 // In late sequence or sequence wrapped
	) {
		client->aplay_seq_next = seq + 1;

		int size = 0;
		const char *const data = janus_rtp_payload(packet->buffer, packet->length, &size);
		if (data == NULL || size <= 0) {
			return;
		}

		us_ring_s *const ring = client->aplay_enc_ring;
		const int ri = us_ring_producer_acquire(ring, 0);
		if (ri < 0) {
			// US_JLOG_ERROR("client", "Session %p aplay ring is full", client->session);
			return;
		}
		us_au_encoded_s *enc = ring->items[ri];
		if ((uz)size < US_ARRAY_LEN(enc->data)) {
			memcpy(enc->data, data, size);
			enc->used = size;
		} else {
			enc->used = 0;
		}
		us_ring_producer_release(ring, ri);
	}
}

static void *_video_thread(void *v_client) {
	US_THREAD_SETTLE("us_cx_vid");
	return _video_or_acap_thread(v_client, true);
}

static void *_acap_thread(void *v_client) {
	US_THREAD_SETTLE("us_cx_ac");
	return _video_or_acap_thread(v_client, false);
}

static void *_video_or_acap_thread(void *v_client, bool video) {
	us_janus_client_s *const client = v_client;
	us_ring_s *const ring = (video ? client->video_ring : client->acap_ring);
	assert(ring != NULL);

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
			&& (video || atomic_load(&client->transmit_acap))
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
				uint video_orient = atomic_load(&client->video_orient);
				if (video_orient != 0) {
					// The extension rotates the video clockwise, but want it counterclockwise.
					// It's more intuitive for people who have seen a protractor at least once in their life.
					if (video_orient == 90) {
						video_orient = 270;
					} else if (video_orient == 270) {
						video_orient = 90;
					}
					packet.extensions.video_rotation = video_orient;
				}
			}

			client->gw->relay_rtp(client->session, &packet);
		}
	}
	return NULL;
}

static void *_aplay_thread(void *v_client) {
	US_THREAD_SETTLE("us_cx_ap");

	us_janus_client_s *const client = v_client;

	int err;
	OpusDecoder *dec = opus_decoder_create(US_RTP_OPUS_HZ, US_RTP_OPUS_CH, &err);
	assert(err == 0);

	while (!atomic_load(&client->stop)) {
		const int in_ri = us_ring_consumer_acquire(client->aplay_enc_ring, 0.1);
		if (in_ri < 0) {
			continue;
		}
		us_au_encoded_s *in = client->aplay_enc_ring->items[in_ri];

		if (in->used == 0) {
			us_ring_consumer_release(client->aplay_enc_ring, in_ri);
			continue;
		}

		const int out_ri = us_ring_producer_acquire(client->aplay_pcm_ring, 0);
		if (out_ri < 0) {
			US_JLOG_ERROR("aplay", "OPUS decoder queue is full");
			us_ring_consumer_release(client->aplay_enc_ring, in_ri);
			continue;
		}
		us_au_pcm_s *out = client->aplay_pcm_ring->items[out_ri];

		const int frames = opus_decode(dec, in->data, in->used, out->data, US_AU_HZ_TO_FRAMES(US_RTP_OPUS_HZ), 0);
		us_ring_consumer_release(client->aplay_enc_ring, in_ri);

		if (frames > 0) {
			out->frames = frames;
		} else {
			out->frames = 0;
			US_JLOG_PERROR_OPUS(frames, "aplay", "Fatal: Can't decode OPUS to PCM frame");
		}
		us_ring_producer_release(client->aplay_pcm_ring, out_ri);
	}

	opus_decoder_destroy(dec);
	return NULL;
}
