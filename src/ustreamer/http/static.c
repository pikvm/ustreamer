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


#include "static.h"


char *us_find_static_file_path(const char *root_path, const char *request_path) {
	char *path = NULL;

	char *const simplified_path = us_simplify_request_path(request_path);
	if (simplified_path[0] == '\0') {
		US_LOG_VERBOSE("HTTP: Invalid request path %s to static", request_path);
		goto error;
	}

	US_CALLOC(path, strlen(root_path) + strlen(simplified_path) + 16); // + reserved for /index.html
	sprintf(path, "%s/%s", root_path, simplified_path);

	struct stat st;
#	define LOAD_STAT { \
			if (lstat(path, &st) < 0) { \
				US_LOG_VERBOSE_PERROR("HTTP: Can't stat() static path %s", path); \
				goto error; \
			} \
		}

	LOAD_STAT;
	if (S_ISDIR(st.st_mode)) {
		US_LOG_VERBOSE("HTTP: Requested static path %s is a directory, trying %s/index.html", path, path);
		strcat(path, "/index.html");
		LOAD_STAT;
	}

#	undef LOAD_STAT

	if (!S_ISREG(st.st_mode)) {
		US_LOG_VERBOSE("HTTP: Not a regular file: %s", path);
		goto error;
	}

	if (access(path, R_OK) < 0) {
		US_LOG_VERBOSE_PERROR("HTTP: Can't access() R_OK file %s", path);
		goto error;
	}

	goto ok;

	error:
		US_DELETE(path, free);
		path = NULL;

	ok:
		free(simplified_path);

	return path;
}
