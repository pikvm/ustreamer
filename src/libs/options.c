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


#include "options.h"

#include <string.h>
#include <ctype.h>
#include <getopt.h>
#include <assert.h>

#include "types.h"


void us_build_short_options(const struct option opts[], char *short_opts, uz size) {
	memset(short_opts, 0, size);
    for (uint short_i = 0, opt_i = 0; opts[opt_i].name != NULL; ++opt_i) {
		assert(short_i < size - 3);
        if (isalpha(opts[opt_i].val)) {
            short_opts[short_i] = opts[opt_i].val;
            ++short_i;
            if (opts[opt_i].has_arg == required_argument) {
                short_opts[short_i] = ':';
                ++short_i;
            }
        }
    }
}
