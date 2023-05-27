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

#include <assert.h>


#define US_LIST_STRUCT(...) \
	__VA_ARGS__ *prev; \
	__VA_ARGS__ *next;

#define US_LIST_ITERATE(x_first, x_item, ...) { \
		for (__typeof__(x_first) x_item = x_first; x_item;) { \
			__typeof__(x_first) m_next = x_item->next; \
			__VA_ARGS__ \
			x_item = m_next; \
		} \
	}

#define US_LIST_APPEND(x_first, x_item) { \
		if (x_first == NULL) { \
			x_first = x_item; \
		} else { \
			__typeof__(x_first) m_last = x_first; \
			for (; m_last->next; m_last = m_last->next); \
			x_item->prev = m_last; \
			m_last->next = x_item; \
		} \
	}

#define US_LIST_APPEND_C(x_first, x_item, x_count) { \
		US_LIST_APPEND(x_first, x_item); \
		++(x_count); \
	}

#define US_LIST_REMOVE(x_first, x_item) { \
		if (x_item->prev == NULL) { \
			x_first = x_item->next; \
		} else { \
			x_item->prev->next = x_item->next; \
		} \
		if (x_item->next != NULL) { \
			x_item->next->prev = x_item->prev; \
		} \
	}

#define US_LIST_REMOVE_C(x_first, x_item, x_count) { \
		US_LIST_REMOVE(x_first, x_item); \
		assert((x_count) >= 1); \
		--(x_count); \
	}
