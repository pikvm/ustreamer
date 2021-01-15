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


#include "file.h"


output_file_s *output_file_init(const char *path, bool json) {
	output_file_s *output;
	A_CALLOC(output, 1);

	if (!strcmp(path, "-")) {
		LOG_INFO("Using output: <stdout>");
		output->fp = stdout;
	} else {
		LOG_INFO("Using output: %s", path);
		if ((output->fp = fopen(path, "wb")) == NULL) {
			LOG_PERROR("Can't open output file");
			goto error;
		}
	}

	output->json = json;
	return output;

	error:
		output_file_destroy(output);
		return NULL;
}

void output_file_write(void *v_output, const frame_s *frame) {
	output_file_s *output = (output_file_s *)v_output;
	if (output->json) {
		base64_encode(frame->data, frame->used, &output->base64_data, &output->base64_allocated);
		fprintf(output->fp,
			"{\"size\": %zu, \"width\": %u, \"height\": %u,"
			" \"format\": %u, \"stride\": %u, \"online\": %u,"
			" \"grab_ts\": %.3Lf, \"encode_begin_ts\": %.3Lf, \"encode_end_ts\": %.3Lf,"
			" \"data\": \"%s\"}\n",
			frame->used, frame->width, frame->height,
			frame->format, frame->stride, frame->online,
			frame->grab_ts, frame->encode_begin_ts, frame->encode_end_ts,
			output->base64_data);
	} else {
		fwrite(frame->data, 1, frame->used, output->fp);
	}
	fflush(output->fp);
}

void output_file_destroy(void *v_output) {
	output_file_s *output = (output_file_s *)v_output;
	if (output->base64_data) {
		free(output->base64_data);
	}
	if (output->fp && output->fp != stdout) {
		if (fclose(output->fp) < 0) {
			LOG_PERROR("Can't close output file");
		}
	}
	free(output);
}
