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

#include "tools.h"


#define US_ARRAY_LEN(x_array) (sizeof(x_array) / sizeof((x_array)[0]))

#define US_ARRAY_ITERATE(x_array, x_start, x_item_ptr, ...) { \
		const int m_len = US_ARRAY_LEN(x_array); \
		US_A(x_start <= m_len); \
		for (int m_i = x_start; m_i < m_len; ++m_i) { \
			__typeof__((x_array)[0]) *const x_item_ptr = &x_array[m_i]; \
			__VA_ARGS__ \
		} \
	}
