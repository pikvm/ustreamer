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

#include <sys/stat.h>

#include <event2/http.h>
#include <event2/keyvalq_struct.h>

#include "../../libs/types.h"


evutil_socket_t us_evhttp_bind_unix(struct evhttp *http, const char *path, bool rm, mode_t mode);

const char *us_evhttp_get_header(struct evhttp_request *request, const char *key);
char *us_evhttp_get_hostport(struct evhttp_request *request);

bool us_evkeyvalq_get_true(struct evkeyvalq *params, const char *key);
char *us_evkeyvalq_get_string(struct evkeyvalq *params, const char *key);

char *us_bufferevent_format_reason(short what);
