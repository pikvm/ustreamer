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


#include "encoder.h"

#include <stdlib.h>
#include <stdbool.h>
#include <strings.h>
#include <assert.h>

#include <linux/videodev2.h>

#include "../common/tools.h"
#include "../common/threading.h"
#include "../common/logging.h"

#include "device.h"

#include "encoders/cpu/encoder.h"
#include "encoders/hw/encoder.h"
#ifdef WITH_OMX
#	include "encoders/omx/encoder.h"
#endif


static const struct {
	const char *name;
	const enum encoder_type_t type;
} _ENCODER_TYPES[] = {
	{"CPU",		ENCODER_TYPE_CPU},
	{"HW",		ENCODER_TYPE_HW},
#	ifdef WITH_OMX
	{"OMX",		ENCODER_TYPE_OMX},
#	endif
#	ifdef WITH_RAWSINK
	{"NOOP",	ENCODER_TYPE_NOOP},
#	endif
};


struct encoder_t *encoder_init(void) {
	struct encoder_runtime_t *run;
	struct encoder_t *encoder;

	A_CALLOC(run, 1);
	run->type = ENCODER_TYPE_CPU;
	run->quality = 80;
	A_MUTEX_INIT(&run->mutex);

	A_CALLOC(encoder, 1);
	encoder->type = run->type;
	encoder->quality = run->quality;
	encoder->run = run;
	return encoder;
}

void encoder_destroy(struct encoder_t *encoder) {
#	ifdef WITH_OMX
	if (encoder->run->omxs) {
		for (unsigned index = 0; index < encoder->run->n_omxs; ++index) {
			if (encoder->run->omxs[index]) {
				omx_encoder_destroy(encoder->run->omxs[index]);
			}
		}
		free(encoder->run->omxs);
	}
#	endif
	A_MUTEX_DESTROY(&encoder->run->mutex);
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

void encoder_prepare(struct encoder_t *encoder, struct device_t *dev) {
	enum encoder_type_t type = (encoder->run->cpu_forced ? ENCODER_TYPE_CPU : encoder->type);
	unsigned quality = encoder->quality;
	bool cpu_forced = false;

	if ((dev->run->format == V4L2_PIX_FMT_MJPEG || dev->run->format == V4L2_PIX_FMT_JPEG) && type != ENCODER_TYPE_HW) {
		LOG_INFO("Switching to HW encoder because the input format is (M)JPEG");
		type = ENCODER_TYPE_HW;
	}

	if (type == ENCODER_TYPE_HW) {
		if (dev->run->format != V4L2_PIX_FMT_MJPEG && dev->run->format != V4L2_PIX_FMT_JPEG) {
			LOG_INFO("Switching to CPU encoder because the input format is not (M)JPEG");
			goto use_cpu;
		}

		if (hw_encoder_prepare(dev, quality) < 0) {
			quality = 0;
		}

		dev->run->n_workers = 1;
	}
#	ifdef WITH_OMX
	else if (type == ENCODER_TYPE_OMX) {
		for (unsigned index = 0; index < encoder->n_glitched_resolutions; ++index) {
			if (
				encoder->glitched_resolutions[index][0] == dev->run->width
				&& encoder->glitched_resolutions[index][1] == dev->run->height
			) {
				LOG_INFO("Switching to CPU encoder the resolution %ux%u marked as glitchy for OMX",
					dev->run->width, dev->run->height);
				goto use_cpu;
			}
		}

		LOG_DEBUG("Preparing OMX encoder ...");

		if (dev->run->n_workers > OMX_MAX_ENCODERS) {
			LOG_INFO("OMX encoder sets limit for worker threads: %u", OMX_MAX_ENCODERS);
			dev->run->n_workers = OMX_MAX_ENCODERS;
		}

		if (encoder->run->omxs == NULL) {
			A_CALLOC(encoder->run->omxs, OMX_MAX_ENCODERS);
		}

		// Начинаем с нуля и доинициализируем на следующих заходах при необходимости
		for (; encoder->run->n_omxs < dev->run->n_workers; ++encoder->run->n_omxs) {
			if ((encoder->run->omxs[encoder->run->n_omxs] = omx_encoder_init()) == NULL) {
				LOG_ERROR("Can't initialize OMX encoder, falling back to CPU");
				goto force_cpu;
			}
		}

		for (unsigned index = 0; index < encoder->run->n_omxs; ++index) {
			if (omx_encoder_prepare(encoder->run->omxs[index], dev, quality) < 0) {
				LOG_ERROR("Can't prepare OMX encoder, falling back to CPU");
				goto force_cpu;
			}
		}
	}
#	endif

	goto ok;

#	pragma GCC diagnostic ignored "-Wunused-label"
#	pragma GCC diagnostic push
	// cppcheck-suppress unusedLabel
	force_cpu:
		cpu_forced = true;
#	pragma GCC diagnostic pop

	use_cpu:
		type = ENCODER_TYPE_CPU;
		quality = encoder->quality;

	ok:
		if (quality == 0) {
			LOG_INFO("Using JPEG quality: encoder default");
#		ifdef WITH_RAWSINK
		} else if (type == ENCODER_TYPE_NOOP) {
			LOG_INFO("Using JPEG NOOP encoder");
#		endif
		} else {
			LOG_INFO("Using JPEG quality: %u%%", quality);
		}

		A_MUTEX_LOCK(&encoder->run->mutex);
		encoder->run->type = type;
		encoder->run->quality = quality;
		if (cpu_forced) {
			encoder->run->cpu_forced = true;
		}
		A_MUTEX_UNLOCK(&encoder->run->mutex);
}

#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic push
int encoder_compress_buffer(struct encoder_t *encoder, struct device_t *dev, unsigned worker_number, unsigned buf_index) {
#pragma GCC diagnostic pop

	assert(encoder->run->type != ENCODER_TYPE_UNKNOWN);
	assert(dev->run->hw_buffers[buf_index].used > 0);

	dev->run->pictures[buf_index]->encode_begin_ts = get_now_monotonic();

	if (encoder->run->type == ENCODER_TYPE_CPU) {
		LOG_VERBOSE("Compressing buffer %u using CPU", buf_index);
		cpu_encoder_compress_buffer(dev, buf_index, encoder->run->quality);
	} else if (encoder->run->type == ENCODER_TYPE_HW) {
		LOG_VERBOSE("Compressing buffer %u using HW (just copying)", buf_index);
		hw_encoder_compress_buffer(dev, buf_index);
	}
#	ifdef WITH_OMX
	else if (encoder->run->type == ENCODER_TYPE_OMX) {
		LOG_VERBOSE("Compressing buffer %u using OMX", buf_index);
		if (omx_encoder_compress_buffer(encoder->run->omxs[worker_number], dev, buf_index) < 0) {
			goto error;
		}
	}
#	endif
#	ifdef WITH_RAWSINK
	else if (encoder->run->type == ENCODER_TYPE_NOOP) {
		LOG_VERBOSE("Compressing buffer %u using NOOP (do nothing)", buf_index);
	}
#	endif


	dev->run->pictures[buf_index]->encode_end_ts = get_now_monotonic();

	dev->run->pictures[buf_index]->width = dev->run->width;
	dev->run->pictures[buf_index]->height = dev->run->height;

	return 0;

#	pragma GCC diagnostic ignored "-Wunused-label"
#	pragma GCC diagnostic push
	// cppcheck-suppress unusedLabel
	error:
		LOG_INFO("Error while compressing buffer, falling back to CPU");
		A_MUTEX_LOCK(&encoder->run->mutex);
		encoder->run->cpu_forced = true;
		A_MUTEX_UNLOCK(&encoder->run->mutex);
		return -1;
#	pragma GCC diagnostic pop
}
