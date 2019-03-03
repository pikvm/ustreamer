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


#include <strings.h>
#include <assert.h>

#include <linux/videodev2.h>

#include "tools.h"
#include "logging.h"
#include "device.h"
#include "encoder.h"

#include "jpeg/encoder.h"
#include "hw/encoder.h"

#ifdef OMX_ENCODER
#	include "omx/encoder.h"
#endif


static const struct {
	const char *name;
	const enum encoder_type_t type;
} _ENCODER_TYPES[] = {
	{"CPU",	ENCODER_TYPE_CPU},
	{"HW",	ENCODER_TYPE_HW},
#	ifdef OMX_ENCODER
	{"OMX",	ENCODER_TYPE_OMX},
#	endif
};


struct encoder_t *encoder_init() {
	struct encoder_runtime_t *run;
	struct encoder_t *encoder;

	A_CALLOC(run, 1);
	run->type = ENCODER_TYPE_CPU;
	run->quality = 80;
	A_PTHREAD_M_INIT(&run->mutex);

	A_CALLOC(encoder, 1);
	encoder->type = run->type;
	encoder->quality = run->quality;
	encoder->run = run;
	return encoder;
}

#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic push
void encoder_prepare(struct encoder_t *encoder, struct device_t *dev) {
#pragma GCC diagnostic pop

	assert(encoder->type != ENCODER_TYPE_UNKNOWN);
	// XXX: Тут нет гонки, потому что encoder_prepare() запускается еще до существования других потоков
	encoder->run->type = encoder->type;
	encoder->run->quality = encoder->quality;

	LOG_INFO("Using JPEG quality: %u%%", encoder->quality);

#	ifdef OMX_ENCODER
	if (encoder->run->type == ENCODER_TYPE_OMX) {
		LOG_DEBUG("Preparing OMX encoder ...");

		if (dev->n_workers > OMX_MAX_ENCODERS) {
			LOG_INFO(
				"OMX-based encoder can only work with %u worker threads; forced --workers=%u",
				OMX_MAX_ENCODERS, OMX_MAX_ENCODERS
			);
			dev->n_workers = OMX_MAX_ENCODERS;
		}
		encoder->run->n_omxs = dev->n_workers;

		A_CALLOC(encoder->run->omxs, encoder->run->n_omxs);
		for (unsigned index = 0; index < encoder->run->n_omxs; ++index) {
			if ((encoder->run->omxs[index] = omx_encoder_init()) == NULL) {
				goto use_fallback;
			}
		}
	}
#	endif

	return;

#	pragma GCC diagnostic ignored "-Wunused-label"
#	pragma GCC diagnostic push
	use_fallback:
		LOG_ERROR("Can't initialize selected encoder, using CPU instead it");
		encoder->run->type = ENCODER_TYPE_CPU;
		encoder->run->quality = encoder->quality;
#	pragma GCC diagnostic pop
}

void encoder_destroy(struct encoder_t *encoder) {
#	ifdef OMX_ENCODER
	if (encoder->run->omxs) {
		for (unsigned index = 0; index < encoder->run->n_omxs; ++index) {
			if (encoder->run->omxs[index]) {
				omx_encoder_destroy(encoder->run->omxs[index]);
			}
		}
		free(encoder->run->omxs);
	}
#	endif
	A_PTHREAD_M_DESTROY(&encoder->run->mutex);
	free(encoder->run);
	free(encoder);
}

enum encoder_type_t encoder_parse_type(const char *str) {
	for (unsigned index = 0; index < ARRAY_LEN(_ENCODER_TYPES); ++index) {
		if (!strcasecmp(str, _ENCODER_TYPES[index].name)) {
			return _ENCODER_TYPES[index].type;
		}
	}
	return ENCODER_TYPE_UNKNOWN;
}

const char *encoder_type_to_string(enum encoder_type_t type) {
	for (unsigned index = 0; index < ARRAY_LEN(_ENCODER_TYPES); ++index) {
		if (_ENCODER_TYPES[index].type == type) {
			return _ENCODER_TYPES[index].name;
		}
	}
	return _ENCODER_TYPES[0].name;
}

void encoder_prepare_live(struct encoder_t *encoder, struct device_t *dev) {
	assert(encoder->run->type != ENCODER_TYPE_UNKNOWN);

	if (
		(dev->run->format == V4L2_PIX_FMT_MJPEG || dev->run->format == V4L2_PIX_FMT_JPEG)
		&& encoder->run->type != ENCODER_TYPE_HW
	) {
		LOG_INFO("Switching to HW encoder because the input format is (M)JPEG");
		A_PTHREAD_M_LOCK(&encoder->run->mutex);
		encoder->run->type = ENCODER_TYPE_HW;
		A_PTHREAD_M_UNLOCK(&encoder->run->mutex);
	}

	if (encoder->run->type == ENCODER_TYPE_HW) {
		if (dev->run->format != V4L2_PIX_FMT_MJPEG && dev->run->format != V4L2_PIX_FMT_JPEG) {
			LOG_INFO("Switching to CPU encoder because the input format is not (M)JPEG");
			goto use_fallback;
		}
		if (hw_encoder_prepare_live(dev, encoder->quality) < 0) {
			A_PTHREAD_M_LOCK(&encoder->run->mutex);
			encoder->run->quality = 0;
			A_PTHREAD_M_UNLOCK(&encoder->run->mutex);
			LOG_INFO("Using JPEG quality: HW default");
		}
	}
#	ifdef OMX_ENCODER
	else if (encoder->run->type == ENCODER_TYPE_OMX) {
		for (unsigned index = 0; index < encoder->run->n_omxs; ++index) {
			if (omx_encoder_prepare_live(encoder->run->omxs[index], dev, encoder->quality) < 0) {
				LOG_ERROR("Can't prepare OMX encoder, falling back to CPU");
				goto use_fallback;
			}
		}
	}
#	endif

	return;

	use_fallback:
		A_PTHREAD_M_LOCK(&encoder->run->mutex);
		encoder->run->type = ENCODER_TYPE_CPU;
		encoder->run->quality = encoder->quality;
		A_PTHREAD_M_UNLOCK(&encoder->run->mutex);
}

#pragma GCC diagnostic ignored "-Wunused-label"
#pragma GCC diagnostic push
int encoder_compress_buffer(struct encoder_t *encoder, struct device_t *dev, unsigned worker_number, unsigned buf_index) {
#pragma GCC diagnostic pop

	assert(encoder->run->type != ENCODER_TYPE_UNKNOWN);

	if (encoder->run->type == ENCODER_TYPE_CPU) {
		jpeg_encoder_compress_buffer(dev, buf_index, encoder->quality);
	} else if (encoder->run->type == ENCODER_TYPE_HW) {
		hw_encoder_compress_buffer(dev, buf_index);
	}
#	ifdef OMX_ENCODER
	else if (encoder->run->type == ENCODER_TYPE_OMX) {
		if (omx_encoder_compress_buffer(encoder->run->omxs[worker_number], dev, buf_index) < 0) {
			goto use_fallback;
		}
	}
#	endif

	return 0;

#	pragma GCC diagnostic ignored "-Wunused-label"
#	pragma GCC diagnostic push
	use_fallback:
		LOG_INFO("Error while compressing, falling back to CPU");
		A_PTHREAD_M_LOCK(&encoder->run->mutex);
		encoder->run->type = ENCODER_TYPE_CPU;
		encoder->run->quality = encoder->quality;
		A_PTHREAD_M_UNLOCK(&encoder->run->mutex);
		return -1;
#	pragma GCC diagnostic pop
}
