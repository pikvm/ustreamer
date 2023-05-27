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


#include "blank.h"


static us_frame_s *_init_internal(void);
static us_frame_s *_init_external(const char *path);


us_frame_s *us_blank_frame_init(const char *path) {
	us_frame_s *blank = NULL;

	if (path && path[0] != '\0') {
		blank = _init_external(path);
	}

	if (blank != NULL) {
		US_LOG_INFO("Using external blank placeholder: %s", path);
	} else {
		blank = _init_internal();
		US_LOG_INFO("Using internal blank placeholder");
	}
	return blank;
}

static us_frame_s *_init_internal(void) {
	us_frame_s *const blank = us_frame_init();
	us_frame_set_data(blank, US_BLANK_JPEG_DATA, US_BLANK_JPEG_DATA_SIZE);
	blank->width = US_BLANK_JPEG_WIDTH;
	blank->height = US_BLANK_JPEG_HEIGHT;
	blank->format = V4L2_PIX_FMT_JPEG;
	return blank;
}

static us_frame_s *_init_external(const char *path) {
	FILE *fp = NULL;

	us_frame_s *blank = us_frame_init();
	blank->format = V4L2_PIX_FMT_JPEG;

	if ((fp = fopen(path, "rb")) == NULL) {
		US_LOG_PERROR("Can't open blank placeholder '%s'", path);
		goto error;
	}

#	define CHUNK_SIZE ((size_t)(100 * 1024))
	while (true) {
		if (blank->used + CHUNK_SIZE >= blank->allocated) {
			us_frame_realloc_data(blank, blank->used + CHUNK_SIZE * 2);
		}

		const size_t readed = fread(blank->data + blank->used, 1, CHUNK_SIZE, fp);
		blank->used += readed;

		if (readed < CHUNK_SIZE) {
			if (feof(fp)) {
				break;
			} else {
				US_LOG_PERROR("Can't read blank placeholder");
				goto error;
			}
		}
	}
#	undef CHUNK_SIZE

	us_frame_s *const decoded = us_frame_init();
	if (us_unjpeg(blank, decoded, false) < 0) {
		us_frame_destroy(decoded);
		goto error;
	}
	blank->width = decoded->width;
	blank->height = decoded->height;
	us_frame_destroy(decoded);

	goto ok;

	error:
		us_frame_destroy(blank);
		blank = NULL;

	ok:
		US_DELETE(fp, fclose);

	return blank;
}
