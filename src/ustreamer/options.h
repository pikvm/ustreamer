/*****************************************************************************
#                                                                            #
#    uStreamer - Lightweight and fast MJPG-HTTP streamer.                    #
#                                                                            #
#    Copyright (C) 2018-2021  Maxim Devaev <mdevaev@gmail.com>               #
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
#include <limits.h>
#include <getopt.h>
#include <errno.h>
#include <assert.h>

#include "../libs/config.h"
#include "../libs/logging.h"
#include "../libs/process.h"
#include "../libs/frame.h"
#include "../libs/memsink.h"
#include "../libs/options.h"

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
	memsink_s	*sink;
	memsink_s	*raw_sink;
#	ifdef WITH_OMX
	memsink_s	*h264_sink;
#	endif
} options_s;


options_s *options_init(unsigned argc, char *argv[]);
void options_destroy(options_s *options);

int options_parse(options_s *options, device_s *dev, encoder_s *enc, stream_s *stream, server_s *server);
