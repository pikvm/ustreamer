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


#include "path.h"


char *us_simplify_request_path(const char *str) {
	// Based on Lighttpd sources:
	//   - https://github.com/lighttpd/lighttpd1.4/blob/b31e7840d5403bc640579135b7004793b9ccd6c0/src/buffer.c#L840
	//   - https://github.com/lighttpd/lighttpd1.4/blob/77c01f981725512653c01cde5ca74c11633dfec4/src/t/test_buffer.c

	char ch; // Current character
	char pre1; // The one before
	char pre2; // The one before that
	char *simplified;
	char *start;
	char *out;
	char *slash;

	US_CALLOC(simplified, strlen(str) + 1);

	if (str[0] == '\0') {
		simplified[0] = '\0';
		return simplified;
	}

	start = simplified;
	out = simplified;
	slash = simplified;

	// Skip leading spaces
	for (; *str == ' '; ++str);

	if (*str == '.') {
		if (str[1] == '/' || str[1] == '\0') {
			++str;
		} else if (str[1] == '.' && (str[2] == '/' || str[2] == '\0')) {
			str += 2;
		}
	}

	pre1 = '\0';
	ch = *(str++);

	while (ch != '\0') {
		pre2 = pre1;
		pre1 = ch;

		// Possibly: out == str - need to read first
		ch = *str;
		*out = pre1;

		out++;
		str++;
		// (out <= str) still true; also now (slash < out)

		if (ch == '/' || ch == '\0') {
			const size_t toklen = out - slash;

			if (toklen == 3 && pre2 == '.' && pre1 == '.' && *slash == '/') {
				// "/../" or ("/.." at end of string)
				out = slash;
				// If there is something before "/..", there is at least one
				// component, which needs to be removed
				if (out > start) {
					--out;
					for (; out > start && *out != '/'; --out);
				}

				// Don't kill trailing '/' at end of path
				if (ch == '\0') {
					++out;
				}
				// slash < out before, so out_new <= slash + 1 <= out_before <= str
			} else if (toklen == 1 || (pre2 == '/' && pre1 == '.')) {
				// "//" or "/./" or (("/" or "/.") at end of string)
				out = slash;
				// Don't kill trailing '/' at end of path
				if (ch == '\0') {
					++out;
				}
				// Slash < out before, so out_new <= slash + 1 <= out_before <= str
			}

			slash = out;
		}
	}

	*out = '\0';
	return simplified;
}

#ifdef TEST_HTTP_PATH

int test_simplify_request_path(const char *sample, const char *expected) {
	char *result = us_simplify_request_path(sample);
	int retval = -!!strcmp(result, expected);

	printf("Testing '%s' -> '%s' ... ", sample, expected);
	if (retval == 0) {
		printf("ok\n");
	} else {
		printf("FAILED; got '%s'\n", result);
	}
	free(result);
	return retval;
}

int main(void) {
	int retval = 0;

#	define TEST_SIMPLIFY_REQUEST_PATH(_sample, _expected) { \
			retval += test_simplify_request_path(_sample, _expected); \
		}

	TEST_SIMPLIFY_REQUEST_PATH("", "");
	TEST_SIMPLIFY_REQUEST_PATH("   ", "");
	TEST_SIMPLIFY_REQUEST_PATH("/", "/");
	TEST_SIMPLIFY_REQUEST_PATH("//", "/");
	TEST_SIMPLIFY_REQUEST_PATH("abc", "abc");
	TEST_SIMPLIFY_REQUEST_PATH("abc//", "abc/");
	TEST_SIMPLIFY_REQUEST_PATH("abc/./xyz", "abc/xyz");
	TEST_SIMPLIFY_REQUEST_PATH("abc/.//xyz", "abc/xyz");
	TEST_SIMPLIFY_REQUEST_PATH("abc/../xyz", "/xyz");
	TEST_SIMPLIFY_REQUEST_PATH("/abc/./xyz", "/abc/xyz");
	TEST_SIMPLIFY_REQUEST_PATH("/abc//./xyz", "/abc/xyz");
	TEST_SIMPLIFY_REQUEST_PATH("/abc/../xyz", "/xyz");
	TEST_SIMPLIFY_REQUEST_PATH("abc/../xyz/.", "/xyz/");
	TEST_SIMPLIFY_REQUEST_PATH("/abc/../xyz/.", "/xyz/");
	TEST_SIMPLIFY_REQUEST_PATH("abc/./xyz/..", "abc/");
	TEST_SIMPLIFY_REQUEST_PATH("/abc/./xyz/..", "/abc/");
	TEST_SIMPLIFY_REQUEST_PATH(".", "");
	TEST_SIMPLIFY_REQUEST_PATH("..", "");
	TEST_SIMPLIFY_REQUEST_PATH("...", "...");
	TEST_SIMPLIFY_REQUEST_PATH("....", "....");
	TEST_SIMPLIFY_REQUEST_PATH(".../", ".../");
	TEST_SIMPLIFY_REQUEST_PATH("./xyz/..", "/");
	TEST_SIMPLIFY_REQUEST_PATH(".//xyz/..", "/");
	TEST_SIMPLIFY_REQUEST_PATH("/./xyz/..", "/");
	TEST_SIMPLIFY_REQUEST_PATH(".././xyz/..", "/");
	TEST_SIMPLIFY_REQUEST_PATH("/.././xyz/..", "/");
	TEST_SIMPLIFY_REQUEST_PATH("/.././xyz/..", "/");
	TEST_SIMPLIFY_REQUEST_PATH("../../../etc/passwd", "/etc/passwd");
	TEST_SIMPLIFY_REQUEST_PATH("/../../../etc/passwd", "/etc/passwd");
	TEST_SIMPLIFY_REQUEST_PATH("   ../../../etc/passwd", "/etc/passwd");
	TEST_SIMPLIFY_REQUEST_PATH("   /../../../etc/passwd", "/etc/passwd");
	TEST_SIMPLIFY_REQUEST_PATH("   /foo/bar/../../../etc/passwd", "/etc/passwd");

#	undef TEST_SIMPLIFY_REQUEST_PATH

	if (retval < 0) {
		printf("===== TEST FAILED =====\n");
	}
	return retval;
}

#endif
