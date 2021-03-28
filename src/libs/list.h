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

#include <assert.h>


#define LIST_STRUCT(...) \
	__VA_ARGS__ *prev; \
	__VA_ARGS__ *next;

#define LIST_ITERATE(_first, _item, ...) { \
		for (__typeof__(_first) _item = _first; _item;) { \
			__typeof__(_first) _next = _item->next; \
			__VA_ARGS__ \
			_item = _next; \
		} \
	}

#define LIST_APPEND(_first, _item) { \
		if (_first == NULL) { \
			_first = _item; \
		} else { \
			__typeof__(_first) _last = _first; \
			for (; _last->next; _last = _last->next); \
			_item->prev = _last; \
			_last->next = _item; \
		} \
	}

#define LIST_APPEND_C(_first, _item, _count) { \
		LIST_APPEND(_first, _item); \
		++(_count); \
	}

#define LIST_REMOVE(_first, _item) { \
		if (_item->prev == NULL) { \
			_first = _item->next; \
		} else { \
			_item->prev->next = _item->next; \
		} \
		if (_item->next != NULL) { \
			_item->next->prev = _item->prev; \
		} \
	}

#define LIST_REMOVE_C(_first, _item, _count) { \
		LIST_REMOVE(_first, _item); \
		assert((_count) >= 1); \
		--(_count); \
	}
