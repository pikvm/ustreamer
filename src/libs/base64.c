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


#include "base64.h"


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


void us_base64_encode(const uint8_t *data, size_t size, char **encoded, size_t *allocated) {
	const size_t encoded_size = 4 * ((size + 2) / 3) + 1; // +1 for '\0'

	if (*encoded == NULL || (allocated && *allocated < encoded_size)) {
		US_REALLOC(*encoded, encoded_size);
		if (allocated) {
			*allocated = encoded_size;
		}
	}

	for (unsigned data_index = 0, encoded_index = 0; data_index < size;) {
#		define OCTET(_name) unsigned _name = (data_index < size ? (uint8_t)data[data_index++] : 0)
		OCTET(octet_a);
		OCTET(octet_b);
		OCTET(octet_c);
#		undef OCTET

		const unsigned triple = (octet_a << 0x10) + (octet_b << 0x08) + octet_c;

#		define ENCODE(_offset) (*encoded)[encoded_index++] = _ENCODING_TABLE[(triple >> _offset * 6) & 0x3F]
		ENCODE(3);
		ENCODE(2);
		ENCODE(1);
		ENCODE(0);
#		undef ENCODE
	}

	for (unsigned index = 0; index < _MOD_TABLE[size % 3]; index++) {
		(*encoded)[encoded_size - 2 - index] = '=';
	}

	(*encoded)[encoded_size - 1] = '\0';
}
