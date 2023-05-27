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


#include "encoder.h"


static const struct {
	const char *name;
	const us_encoder_type_e type; // cppcheck-suppress unusedStructMember
} _ENCODER_TYPES[] = {
	{"CPU",			US_ENCODER_TYPE_CPU},
	{"HW",			US_ENCODER_TYPE_HW},
	{"M2M-VIDEO",	US_ENCODER_TYPE_M2M_VIDEO},
	{"M2M-IMAGE",	US_ENCODER_TYPE_M2M_IMAGE},
	{"M2M-MJPEG",	US_ENCODER_TYPE_M2M_VIDEO},
	{"M2M-JPEG",	US_ENCODER_TYPE_M2M_IMAGE},
	{"OMX",			US_ENCODER_TYPE_M2M_IMAGE},
	{"NOOP",		US_ENCODER_TYPE_NOOP},
};


static void *_worker_job_init(void *v_enc);
static void _worker_job_destroy(void *v_job);
static bool _worker_run_job(us_worker_s *wr);


#define _ER(x_next)	enc->run->x_next


us_encoder_s *us_encoder_init(void) {
	us_encoder_runtime_s *run;
	US_CALLOC(run, 1);
	run->type = US_ENCODER_TYPE_CPU;
	run->quality = 80;
	US_MUTEX_INIT(run->mutex);

	us_encoder_s *enc;
	US_CALLOC(enc, 1);
	enc->type = run->type;
	enc->n_workers = us_get_cores_available();
	enc->run = run;
	return enc;
}

void us_encoder_destroy(us_encoder_s *enc) {
	if (_ER(m2ms) != NULL) {
		for (unsigned index = 0; index < _ER(n_m2ms); ++index) {
			US_DELETE(_ER(m2ms[index]), us_m2m_encoder_destroy)
		}
		free(_ER(m2ms));
	}
	US_MUTEX_DESTROY(_ER(mutex));
	free(enc->run);
	free(enc);
}

us_encoder_type_e us_encoder_parse_type(const char *str) {
	US_ARRAY_ITERATE(_ENCODER_TYPES, 0, item, {
		if (!strcasecmp(item->name, str)) {
			return item->type;
		}
	});
	return US_ENCODER_TYPE_UNKNOWN;
}

const char *us_encoder_type_to_string(us_encoder_type_e type) {
	US_ARRAY_ITERATE(_ENCODER_TYPES, 0, item, {
		if (item->type == type) {
			return item->name;
		}
	});
	return _ENCODER_TYPES[0].name;
}

us_workers_pool_s *us_encoder_workers_pool_init(us_encoder_s *enc, us_device_s *dev) {
#	define DR(x_next) dev->run->x_next

	us_encoder_type_e type = (_ER(cpu_forced) ? US_ENCODER_TYPE_CPU : enc->type);
	unsigned quality = dev->jpeg_quality;
	unsigned n_workers = us_min_u(enc->n_workers, DR(n_bufs));
	bool cpu_forced = false;

	if (us_is_jpeg(DR(format)) && type != US_ENCODER_TYPE_HW) {
		US_LOG_INFO("Switching to HW encoder: the input is (M)JPEG ...");
		type = US_ENCODER_TYPE_HW;
	}

	if (type == US_ENCODER_TYPE_HW) {
		if (!us_is_jpeg(DR(format))) {
			US_LOG_INFO("Switching to CPU encoder: the input format is not (M)JPEG ...");
			goto use_cpu;
		}
		quality = DR(jpeg_quality);
		n_workers = 1;

	} else if (type == US_ENCODER_TYPE_M2M_VIDEO || type == US_ENCODER_TYPE_M2M_IMAGE) {
		US_LOG_DEBUG("Preparing M2M-%s encoder ...", (type == US_ENCODER_TYPE_M2M_VIDEO ? "VIDEO" : "IMAGE"));
		if (_ER(m2ms) == NULL) {
			US_CALLOC(_ER(m2ms), n_workers);
		}
		for (; _ER(n_m2ms) < n_workers; ++_ER(n_m2ms)) {
			// Начинаем с нуля и доинициализируем на следующих заходах при необходимости
			char name[32];
			snprintf(name, 32, "JPEG-%u", _ER(n_m2ms));
			if (type == US_ENCODER_TYPE_M2M_VIDEO) {
				_ER(m2ms[_ER(n_m2ms)]) = us_m2m_mjpeg_encoder_init(name, enc->m2m_path, quality);
			} else {
				_ER(m2ms[_ER(n_m2ms)]) = us_m2m_jpeg_encoder_init(name, enc->m2m_path, quality);
			}
		}

	} else if (type == US_ENCODER_TYPE_NOOP) {
		n_workers = 1;
		quality = 0;
	}

	goto ok;

	use_cpu:
		type = US_ENCODER_TYPE_CPU;
		quality = dev->jpeg_quality;

	ok:
		if (type == US_ENCODER_TYPE_NOOP) {
			US_LOG_INFO("Using JPEG NOOP encoder");
		} else if (quality == 0) {
			US_LOG_INFO("Using JPEG quality: encoder default");
		} else {
			US_LOG_INFO("Using JPEG quality: %u%%", quality);
		}

		US_MUTEX_LOCK(_ER(mutex));
		_ER(type) = type;
		_ER(quality) = quality;
		if (cpu_forced) {
			_ER(cpu_forced) = true;
		}
		US_MUTEX_UNLOCK(_ER(mutex));

		const long double desired_interval = (
			dev->desired_fps > 0 && (dev->desired_fps < dev->run->hw_fps || dev->run->hw_fps == 0)
			? (long double)1 / dev->desired_fps
			: 0
		);

		return us_workers_pool_init(
			"JPEG", "jw", n_workers, desired_interval,
			_worker_job_init, (void *)enc,
			_worker_job_destroy,
			_worker_run_job);

#	undef DR
}

void us_encoder_get_runtime_params(us_encoder_s *enc, us_encoder_type_e *type, unsigned *quality) {
	US_MUTEX_LOCK(_ER(mutex));
	*type = _ER(type);
	*quality = _ER(quality);
	US_MUTEX_UNLOCK(_ER(mutex));
}

static void *_worker_job_init(void *v_enc) {
	us_encoder_job_s *job;
	US_CALLOC(job, 1);
	job->enc = (us_encoder_s *)v_enc;
	job->dest = us_frame_init();
	return (void *)job;
}

static void _worker_job_destroy(void *v_job) {
	us_encoder_job_s *job = (us_encoder_job_s *)v_job;
	us_frame_destroy(job->dest);
	free(job);
}

static bool _worker_run_job(us_worker_s *wr) {
	us_encoder_job_s *job = (us_encoder_job_s *)wr->job;
	us_encoder_s *enc = job->enc; // Just for _ER()
	us_frame_s *src = &job->hw->raw;
	us_frame_s *dest = job->dest;

	assert(_ER(type) != US_ENCODER_TYPE_UNKNOWN);

	if (_ER(type) == US_ENCODER_TYPE_CPU) {
		US_LOG_VERBOSE("Compressing JPEG using CPU: worker=%s, buffer=%u",
			wr->name, job->hw->buf.index);
		us_cpu_encoder_compress(src, dest, _ER(quality));

	} else if (_ER(type) == US_ENCODER_TYPE_HW) {
		US_LOG_VERBOSE("Compressing JPEG using HW (just copying): worker=%s, buffer=%u",
			wr->name, job->hw->buf.index);
		us_hw_encoder_compress(src, dest);

	} else if (_ER(type) == US_ENCODER_TYPE_M2M_VIDEO || _ER(type) == US_ENCODER_TYPE_M2M_IMAGE) {
		US_LOG_VERBOSE("Compressing JPEG using M2M-%s: worker=%s, buffer=%u",
			(_ER(type) == US_ENCODER_TYPE_M2M_VIDEO ? "VIDEO" : "IMAGE"), wr->name, job->hw->buf.index);
		if (us_m2m_encoder_compress(_ER(m2ms[wr->number]), src, dest, false) < 0) {
			goto error;
		}

	} else if (_ER(type) == US_ENCODER_TYPE_NOOP) {
		US_LOG_VERBOSE("Compressing JPEG using NOOP (do nothing): worker=%s, buffer=%u",
			wr->name, job->hw->buf.index);
		us_frame_encoding_begin(src, dest, V4L2_PIX_FMT_JPEG);
		usleep(5000); // Просто чтобы работала логика desired_fps
		dest->encode_end_ts = us_get_now_monotonic(); // us_frame_encoding_end()
	}

	US_LOG_VERBOSE("Compressed new JPEG: size=%zu, time=%0.3Lf, worker=%s, buffer=%u",
		job->dest->used,
		job->dest->encode_end_ts - job->dest->encode_begin_ts,
		wr->name,
		job->hw->buf.index);

	return true;

	error:
		US_LOG_ERROR("Compression failed: worker=%s, buffer=%u", wr->name, job->hw->buf.index);
		US_LOG_ERROR("Error while compressing buffer, falling back to CPU");
		US_MUTEX_LOCK(_ER(mutex));
		_ER(cpu_forced) = true;
		US_MUTEX_UNLOCK(_ER(mutex));
		return false;
}
