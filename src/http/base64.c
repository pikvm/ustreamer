/*****************************************************************************
#                                                                            #
#    uStreamer - Lightweight and fast MJPG-HTTP streamer.                    #
#                                                                            #
#    Copyright (C) 2018  Maxim Devaev <mdevaev@gmail.com>                    #
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


#include "base64.h"

#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "../tools.h"


static const char _ENCODING_TABLE[] = {
	'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H',
	'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P',
	'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X',
	'Y', 'Z', 'a', 'b', 'c', 'd', 'e', 'f',
	'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n',
	'o', 'p', 'q', 'r', 's', 't', 'u', 'v',
	'w', 'x', 'y', 'z', '0', '1', '2', '3',
	'4', '5', '6', '7', '8', '9', '+', '/',
};

static const unsigned _MOD_TABLE[] = {0, 2, 1};


char *base64_encode(const unsigned char *str) {
	size_t str_len = strlen((const char *)str);
	size_t encoded_size = 4 * ((str_len + 2) / 3) + 1; // +1 for '\0'
	char *encoded;

	A_CALLOC(encoded, encoded_size);

	for (unsigned str_index = 0, encoded_index = 0; str_index < str_len;) {
		unsigned octet_a = (str_index < str_len ? (unsigned char)str[str_index++] : 0);
		unsigned octet_b = (str_index < str_len ? (unsigned char)str[str_index++] : 0);
		unsigned octet_c = (str_index < str_len ? (unsigned char)str[str_index++] : 0);

		unsigned triple = (octet_a << 0x10) + (octet_b << 0x08) + octet_c;

		encoded[encoded_index++] = _ENCODING_TABLE[(triple >> 3 * 6) & 0x3F];
		encoded[encoded_index++] = _ENCODING_TABLE[(triple >> 2 * 6) & 0x3F];
		encoded[encoded_index++] = _ENCODING_TABLE[(triple >> 1 * 6) & 0x3F];
		encoded[encoded_index++] = _ENCODING_TABLE[(triple >> 0 * 6) & 0x3F];
	}

	for (unsigned index = 0; index < _MOD_TABLE[str_len % 3]; index++) {
		encoded[encoded_size - 2 - index] = '=';
	}

	encoded[encoded_size - 1] = '\0';
	return encoded;
}
