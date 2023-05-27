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


#include "options.h"


void us_build_short_options(const struct option opts[], char *short_opts, size_t size) {
	memset(short_opts, 0, size);
    for (unsigned short_index = 0, opt_index = 0; opts[opt_index].name != NULL; ++opt_index) {
		assert(short_index < size - 3);
        if (isalpha(opts[opt_index].val)) {
            short_opts[short_index] = opts[opt_index].val;
            ++short_index;
            if (opts[opt_index].has_arg == required_argument) {
                short_opts[short_index] = ':';
                ++short_index;
            }
        }
    }
}
