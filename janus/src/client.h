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


#pragma once

#include <stdlib.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <string.h>

#include <pthread.h>
#include <janus/plugins/plugin.h>

#include "uslibs/tools.h"
#include "uslibs/threading.h"
#include "uslibs/list.h"

#include "logging.h"
#include "queue.h"
#include "rtp.h"


typedef struct us_janus_client_sx {
	janus_callbacks			*gw;
	janus_plugin_session	*session;
	atomic_bool				transmit;
	atomic_bool				transmit_audio;

	pthread_t				video_tid;
	pthread_t				audio_tid;
	atomic_bool				stop;

	us_queue_s				*video_queue;
	us_queue_s				*audio_queue;

    US_LIST_STRUCT(struct us_janus_client_sx);
} us_janus_client_s;


us_janus_client_s *us_janus_client_init(janus_callbacks *gw, janus_plugin_session *session);
void us_janus_client_destroy(us_janus_client_s *client);

void us_janus_client_send(us_janus_client_s *client, const us_rtp_s *rtp);
