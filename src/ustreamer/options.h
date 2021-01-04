/*****************************************************************************
#                                                                            #
#    uStreamer - Lightweight and fast MJPG-HTTP streamer.                    #
#                                                                            #
#    Copyright (C) 2018  Maxim Devaev <mdevaev@gmail.com>                    #
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

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <limits.h>
#include <getopt.h>
#include <errno.h>
#include <assert.h>

#include "../libs/common/config.h"
#include "../libs/common/logging.h"
#include "../libs/common/process.h"
#include "../libs/common/frame.h"
#ifdef WITH_OMX
#	include "../libs/memsink/memsink.h"
#endif

#include "device.h"
#include "encoder.h"
#include "blank.h"
#include "stream.h"
#include "http/server.h"
#ifdef WITH_GPIO
#	include "gpio/gpio.h"
#endif


typedef struct {
	unsigned	argc;
	char		**argv;
	char		**argv_copy;
	frame_s		*blank;
#	ifdef WITH_OMX
	memsink_s	*h264_sink;
#	endif
} options_s;


options_s *options_init(unsigned argc, char *argv[]);
void options_destroy(options_s *options);

int options_parse(options_s *options, device_s *dev, encoder_s *enc, stream_s *stream, server_s *server);
