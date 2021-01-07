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


#include "mime.h"


static const struct {
	const char *ext;
	const char *mime;
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


const char *guess_mime_type(const char *path) {
	// FIXME: false-positive cppcheck
	char *dot = strrchr(path, '.'); // cppcheck-suppress ctunullpointer
	if (dot == NULL || strchr(dot, '/') != NULL) {
		goto misc;
	}

	char *ext = dot + 1;
	for (unsigned index = 0; index < ARRAY_LEN(_MIME_TYPES); ++index) {
		if (!evutil_ascii_strcasecmp(ext, _MIME_TYPES[index].ext)) {
			return _MIME_TYPES[index].mime;
		}
	}

	misc:
		return "application/misc";
}
