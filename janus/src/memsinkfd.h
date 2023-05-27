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

#include <stdbool.h>
#include <stdint.h>
#include <unistd.h>

#include <linux/videodev2.h>

#include "uslibs/tools.h"
#include "uslibs/frame.h"
#include "uslibs/memsinksh.h"

#include "logging.h"


int us_memsink_fd_wait_frame(int fd, us_memsink_shared_s* mem, uint64_t last_id);
us_frame_s *us_memsink_fd_get_frame(int fd, us_memsink_shared_s *mem, uint64_t *frame_id, bool key_required);
