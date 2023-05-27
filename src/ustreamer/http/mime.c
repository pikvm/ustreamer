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


#include "mime.h"


static const struct {
	const char *ext; // cppcheck-suppress unusedStructMember
	const char *mime; // cppcheck-suppress unusedStructMember
} _MIME_TYPES[] = {
	{"html",	"text/html"},
	{"htm",		"text/html"},
	{"css",		"text/css"},
	{"js",		"text/javascript"},
	{"txt",		"text/plain"},
	{"jpg",		"image/jpeg"},
	{"jpeg",	"image/jpeg"},
	{"png",		"image/png"},
	{"gif",		"image/gif"},
	{"ico",		"image/x-icon"},
	{"bmp",		"image/bmp"},
	{"svg",		"image/svg+xml"},
	{"swf",		"application/x-shockwave-flash"},
	{"cab",		"application/x-shockwave-flash"},
	{"jar",		"application/java-archive"},
	{"json",	"application/json"},
};


const char *us_guess_mime_type(const char *path) {
	// FIXME: false-positive cppcheck
	const char *dot = strrchr(path, '.'); // cppcheck-suppress ctunullpointer
	if (dot == NULL || strchr(dot, '/') != NULL) {
		goto misc;
	}

	const char *ext = dot + 1;
	US_ARRAY_ITERATE(_MIME_TYPES, 0, item, {
		if (!evutil_ascii_strcasecmp(item->ext, ext)) {
			return item->mime;
		}
	});

	misc:
		return "application/misc";
}
