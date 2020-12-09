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


#include "blank.h"


struct _jpeg_error_manager_t {
	struct jpeg_error_mgr	mgr; // Default manager
	jmp_buf					jmp;
};


static struct frame_t *_init_internal(void);
static struct frame_t *_init_external(const char *path);

static int _jpeg_read_geometry(FILE *fp, unsigned *width, unsigned *height);
static void _jpeg_error_handler(j_common_ptr jpeg);


struct frame_t *blank_frame_init(const char *path) {
	struct frame_t *blank = NULL;

	if (path) {
		blank = _init_external(path);
	}

	if (blank) {
		LOG_INFO("Using external blank placeholder: %s", path);
	} else {
		blank = _init_internal();
		LOG_INFO("Using internal blank placeholder");
	}
	return blank;
}

static struct frame_t *_init_internal(void) {
	struct frame_t *blank;

	blank = frame_init("blank_internal");
	frame_set_data(blank, BLANK_JPEG_DATA, BLANK_JPEG_DATA_SIZE);
	blank->width = BLANK_JPEG_WIDTH;
	blank->height = BLANK_JPEG_HEIGHT;
	return blank;
}

static struct frame_t *_init_external(const char *path) {
	FILE *fp = NULL;
	struct frame_t *blank;

	blank = frame_init("blank_external");

	if ((fp = fopen(path, "rb")) == NULL) {
		LOG_PERROR("Can't open blank placeholder '%s'", path);
		goto error;
	}

	if (_jpeg_read_geometry(fp, &blank->width, &blank->height) < 0) {
		goto error;
	}

	if (fseek(fp, 0, SEEK_SET) < 0) {
		LOG_PERROR("Can't seek to begin of the blank placeholder");
		goto error;
	}

#	define CHUNK_SIZE ((size_t)(100 * 1024))
	while (true) {
		if (blank->used + CHUNK_SIZE >= blank->allocated) {
			frame_realloc_data(blank, blank->used + CHUNK_SIZE * 2);
		}

		size_t readed = fread(blank->data + blank->used, 1, CHUNK_SIZE, fp);
		blank->used += readed;

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
		frame_destroy(blank);
		blank = NULL;

	ok:
		if (fp) {
			fclose(fp);
		}

	return blank;
}

static int _jpeg_read_geometry(FILE *fp, unsigned *width, unsigned *height) {
	struct jpeg_decompress_struct jpeg;
	struct _jpeg_error_manager_t jpeg_error;

	jpeg_create_decompress(&jpeg);

	// https://stackoverflow.com/questions/19857766/error-handling-in-libjpeg
	jpeg.err = jpeg_std_error((struct jpeg_error_mgr *)&jpeg_error);
	jpeg_error.mgr.error_exit = _jpeg_error_handler;
	if (setjmp(jpeg_error.jmp) < 0) {
		jpeg_destroy_decompress(&jpeg);
		return -1;
	}

	jpeg_stdio_src(&jpeg, fp);
	jpeg_read_header(&jpeg, TRUE);
	jpeg_start_decompress(&jpeg);

	*width = jpeg.output_width;
	*height = jpeg.output_height;

	jpeg_destroy_decompress(&jpeg);
	return 0;
}

static void _jpeg_error_handler(j_common_ptr jpeg) {
	struct _jpeg_error_manager_t *jpeg_error = (struct _jpeg_error_manager_t *)jpeg->err;
	char msg[JMSG_LENGTH_MAX];

	(*jpeg_error->mgr.format_message)(jpeg, msg);
	LOG_ERROR("Invalid blank placeholder: %s", msg);
	longjmp(jpeg_error->jmp, -1);
}
