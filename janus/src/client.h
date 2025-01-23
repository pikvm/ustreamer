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


#pragma once

#include <stdatomic.h>

#include <pthread.h>
#include <janus/plugins/plugin.h>

#include "uslibs/types.h"
#include "uslibs/list.h"
#include "uslibs/ring.h"

#include "rtp.h"


typedef struct {
	janus_callbacks			*gw;
	janus_plugin_session	*session;
	atomic_bool				transmit;
	atomic_bool				transmit_acap;
	atomic_bool				transmit_aplay;
	atomic_uint				video_orient;

	pthread_t				video_tid;
	pthread_t				acap_tid;
	pthread_t				aplay_tid;
	atomic_bool				stop;

	us_ring_s				*video_ring;
	us_ring_s				*acap_ring;

	us_ring_s				*aplay_enc_ring;
	u16						aplay_seq_next;
	us_ring_s				*aplay_pcm_ring;

    US_LIST_DECLARE;
} us_janus_client_s;


us_janus_client_s *us_janus_client_init(janus_callbacks *gw, janus_plugin_session *session);
void us_janus_client_destroy(us_janus_client_s *client);

void us_janus_client_send(us_janus_client_s *client, const us_rtp_s *rtp);
void us_janus_client_recv(us_janus_client_s *client, janus_plugin_rtp *packet);
