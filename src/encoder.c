/*****************************************************************************
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


#include "tools.h"
#include "device.h"
#include "encoder.h"

#include "jpeg/encoder.h"

#ifdef OMX_ENCODER
#	include "omx/encoder.h"
#endif

struct encoder_t *encoder_init(enum encoder_type_t type) {
	struct encoder_t *encoder;

	A_CALLOC(encoder, 1);
	encoder->type = type;

#	ifdef OMX_ENCODER
	if (type == ENCODER_TYPE_OMX) {
		if ((encoder->omx = omx_encoder_init()) == NULL) {
			goto use_fallback;
		}
	}
#	endif

	return encoder;

#	pragma GCC diagnostic ignored "-Wunused-label"
#	pragma GCC diagnostic push
	use_fallback:
		LOG_ERROR("Can't initialize selected encoder, using CPU instead it");
		encoder->type = ENCODER_TYPE_CPU;
		return encoder;
#	pragma GCC diagnostic pop
}

void encoder_destroy(struct encoder_t *encoder) {
#	ifdef OMX_ENCODER
	if (encoder->omx) {
		omx_encoder_destroy(encoder->omx);
	}
#	endif
	free(encoder);
}

#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic push
void encoder_prepare(struct encoder_t *encoder, struct device_t *dev) {
#pragma GCC diagnostic pop
#	ifdef OMX_ENCODER
	if (encoder->type == ENCODER_TYPE_OMX) {
		if (omx_encoder_prepare(encoder->omx, dev) < 0) {
			goto use_fallback;
		}
	}
	if (dev->run->n_workers > 1) {
		LOG_INFO("OMX encoder can only work with one worker thread; forcing n_workers to 1");
		dev->run->n_workers = 1;
	}
#	endif

	return;

#	pragma GCC diagnostic ignored "-Wunused-label"
#	pragma GCC diagnostic push
	use_fallback:
		LOG_ERROR("Can't prepare selected encoder, falling back to CPU");
		encoder->type = ENCODER_TYPE_CPU;
#	pragma GCC diagnostic pop
}

void encoder_compress_buffer(struct encoder_t *encoder, struct device_t *dev, int index)  {
#	ifdef OMX_ENCODER
	if (encoder->type == ENCODER_TYPE_OMX) {
		if (omx_encoder_compress_buffer(encoder->omx, dev, index) < 0) {
			LOG_INFO("OMX compressor error, falling back to CPU method");
			encoder->type = ENCODER_TYPE_CPU;
		}
	}
#	endif

	if (encoder->type == ENCODER_TYPE_CPU) {
		jpeg_encoder_compress_buffer(dev, index);
	}
}
