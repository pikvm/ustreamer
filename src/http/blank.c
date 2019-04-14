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


#include <stdio.h>
#include <stdbool.h>
#include <string.h>

#include <jpeglib.h>

#include "../tools.h"
#include "../logging.h"

#include "data/blank_jpeg.h"

#include "blank.h"


static struct blank_t *_blank_init_internal();
static struct blank_t *_blank_init_external(const char *path);


struct blank_t *blank_init(const char *path) {
	struct blank_t *blank = NULL;

	if (path) {
		blank = _blank_init_external(path);
	}

	if (blank) {
		LOG_INFO("Using external blank placeholder: %s", path);
	} else {
		blank = _blank_init_internal();
		LOG_INFO("Using internal blank placeholder");
	}
	return blank;
}

void blank_destroy(struct blank_t *blank) {
	free(blank->picture.data);
	free(blank);
}

static struct blank_t *_blank_init_internal() {
	struct blank_t *blank;

	A_CALLOC(blank, 1);

	A_CALLOC(blank->picture.data, ARRAY_LEN(BLANK_JPEG_DATA));
	memcpy(blank->picture.data, BLANK_JPEG_DATA, ARRAY_LEN(BLANK_JPEG_DATA) * sizeof(*blank->picture.data));

	blank->picture.used = ARRAY_LEN(BLANK_JPEG_DATA);
	blank->picture.allocated = ARRAY_LEN(BLANK_JPEG_DATA);

	blank->width = BLANK_JPEG_WIDTH;
	blank->height = BLANK_JPEG_HEIGHT;

	return blank;
}

static struct blank_t *_blank_init_external(const char *path) {
	FILE *fp = NULL;
	struct jpeg_error_mgr jpeg_error;
	struct jpeg_decompress_struct jpeg;
	struct blank_t *blank;

	A_CALLOC(blank, 1);

	if ((fp = fopen(path, "rb")) == NULL) {
		LOG_PERROR("Can't open blank placeholder '%s'", path);
		goto error;
	}

	jpeg_create_decompress(&jpeg);
	jpeg.err = jpeg_std_error(&jpeg_error);
	jpeg_stdio_src(&jpeg, fp);
	jpeg_read_header(&jpeg, TRUE);
	jpeg_start_decompress(&jpeg);

	blank->width = jpeg.output_width;
	blank->height = jpeg.output_height;

	jpeg_destroy_decompress(&jpeg);

	if (fseek(fp, 0, SEEK_SET) < 0) {
		LOG_PERROR("Can't seek to begin of the blank placeholder");
		goto error;
	}

#	define CHUNK_SIZE (100 * 1024)
	while (true) {
		if (blank->picture.used + CHUNK_SIZE >= blank->picture.allocated) {
			blank->picture.allocated = blank->picture.used + CHUNK_SIZE * 2;
			A_REALLOC(blank->picture.data, blank->picture.allocated);
		}

		size_t readed = fread(blank->picture.data + blank->picture.used, sizeof(*blank->picture.data), CHUNK_SIZE, fp);
		blank->picture.used += readed;

		if (readed < CHUNK_SIZE) {
			if (feof(fp)) {
				goto ok;
			} else {
				LOG_PERROR("Can't read blank placeholder");
				goto error;
			}
		}
	}
#	undef CHUNK_SIZE

	error:
		free(blank->picture.data);
		free(blank);
		blank = NULL;

	ok:
		if (fp) {
			fclose(fp);
		}

	return blank;
}
