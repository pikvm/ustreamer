/*****************************************************************************
#                                                                            #
#    uStreamer - Lightweight and fast MJPEG-HTTP streamer.                   #
#                                                                            #
#    Copyright (C) 2018-2024  Maxim Devaev <mdevaev@gmail.com>               #
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


#include "m2m.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <fcntl.h>
#include <poll.h>
#include <errno.h>
#include <assert.h>

#include <sys/mman.h>

#include <linux/videodev2.h>

#include "../libs/types.h"
#include "../libs/tools.h"
#include "../libs/logging.h"
#include "../libs/frame.h"
#include "../libs/xioctl.h"


static us_m2m_encoder_s *_m2m_encoder_init(
	const char *name, const char *path, uint output_format,
	uint bitrate, uint gop, uint quality, bool allow_dma);

static void _m2m_encoder_ensure(us_m2m_encoder_s *enc, const us_frame_s *frame);

static int _m2m_encoder_init_buffers(
	us_m2m_encoder_s *enc, const char *name, enum v4l2_buf_type type,
	us_m2m_buffer_s **bufs_ptr, uint *n_bufs_ptr, bool dma);

static void _m2m_encoder_cleanup(us_m2m_encoder_s *enc);

static int _m2m_encoder_compress_raw(us_m2m_encoder_s *enc, const us_frame_s *src, us_frame_s *dest, bool force_key);


#define _LOG_ERROR(x_msg, ...)	US_LOG_ERROR("%s: " x_msg, enc->name, ##__VA_ARGS__)
#define _LOG_PERROR(x_msg, ...)	US_LOG_PERROR("%s: " x_msg, enc->name, ##__VA_ARGS__)
#define _LOG_INFO(x_msg, ...)		US_LOG_INFO("%s: " x_msg, enc->name, ##__VA_ARGS__)
#define _LOG_VERBOSE(x_msg, ...)	US_LOG_VERBOSE("%s: " x_msg, enc->name, ##__VA_ARGS__)
#define _LOG_DEBUG(x_msg, ...)	US_LOG_DEBUG("%s: " x_msg, enc->name, ##__VA_ARGS__)


us_m2m_encoder_s *us_m2m_h264_encoder_init(const char *name, const char *path, uint bitrate, uint gop) {
	bitrate *= 1000; // From Kbps
	return _m2m_encoder_init(name, path, V4L2_PIX_FMT_H264, bitrate, gop, 0, true);
}

us_m2m_encoder_s *us_m2m_mjpeg_encoder_init(const char *name, const char *path, uint quality) {
	const double b_min = 25;
	const double b_max = 20000;
	const double step = 25;
	double bitrate = log10(quality) * (b_max - b_min) / 2 + b_min;
	bitrate = step * round(bitrate / step);
	bitrate *= 1000; // From Kbps
	assert(bitrate > 0);
	return _m2m_encoder_init(name, path, V4L2_PIX_FMT_MJPEG, bitrate, 0, 0, true);
}

us_m2m_encoder_s *us_m2m_jpeg_encoder_init(const char *name, const char *path, uint quality) {
	// FIXME: DMA не работает
	return _m2m_encoder_init(name, path, V4L2_PIX_FMT_JPEG, 0, 0, quality, false);
}

void us_m2m_encoder_destroy(us_m2m_encoder_s *enc) {
	_LOG_INFO("Destroying encoder ...");
	_m2m_encoder_cleanup(enc);
	free(enc->path);
	free(enc->name);
	free(enc);
}

int us_m2m_encoder_compress(us_m2m_encoder_s *enc, const us_frame_s *src, us_frame_s *dest, bool force_key) {
	us_m2m_encoder_runtime_s *const run = enc->run;

	uint dest_format = enc->output_format;
	switch (enc->output_format) {
		case V4L2_PIX_FMT_JPEG:
			force_key = false;
			// fall through
		case V4L2_PIX_FMT_MJPEG:
			dest_format = V4L2_PIX_FMT_JPEG;
			break;
		case V4L2_PIX_FMT_H264:
			force_key = (
				force_key
				|| run->last_online != src->online
				|| run->last_encode_ts + 0.5 < us_get_now_monotonic()
			);
			break;
	}

	us_frame_encoding_begin(src, dest, dest_format);

	_m2m_encoder_ensure(enc, src);
	if (!run->ready) { // Already prepared but failed
		return -1;
	}

	_LOG_DEBUG("Compressing new frame; force_key=%d ...", force_key);

	if (_m2m_encoder_compress_raw(enc, src, dest, force_key) < 0) {
		_m2m_encoder_cleanup(enc);
		_LOG_ERROR("Encoder destroyed due an error (compress)");
		return -1;
	}

	us_frame_encoding_end(dest);

	_LOG_VERBOSE("Compressed new frame: size=%zu, time=%0.3Lf, force_key=%d",
		dest->used, dest->encode_end_ts - dest->encode_begin_ts, force_key);

	run->last_online = src->online;
	run->last_encode_ts = dest->encode_end_ts;
	return 0;
}

static us_m2m_encoder_s *_m2m_encoder_init(
	const char *name, const char *path, uint output_format,
	uint bitrate, uint gop, uint quality, bool allow_dma) {

	US_LOG_INFO("%s: Initializing encoder ...", name);

	us_m2m_encoder_runtime_s *run;
	US_CALLOC(run, 1);
	run->last_online = -1;
	run->fd = -1;

	us_m2m_encoder_s *enc;
	US_CALLOC(enc, 1);
	enc->name = us_strdup(name);
	if (path == NULL) {
		enc->path = us_strdup(output_format == V4L2_PIX_FMT_JPEG ? "/dev/video31" : "/dev/video11");
	} else {
		enc->path = us_strdup(path);
	}
	enc->output_format = output_format;
	enc->bitrate = bitrate;
	enc->gop = gop;
	enc->quality = quality;
	enc->allow_dma = allow_dma;
	enc->run = run;
	return enc;
}

#define _E_XIOCTL(x_request, x_value, x_msg, ...) { \
		if (us_xioctl(run->fd, x_request, x_value) < 0) { \
			_LOG_PERROR(x_msg, ##__VA_ARGS__); \
			goto error; \
		} \
	}

static void _m2m_encoder_ensure(us_m2m_encoder_s *enc, const us_frame_s *frame) {
	us_m2m_encoder_runtime_s *const run = enc->run;

	const bool dma = (enc->allow_dma && frame->dma_fd >= 0);
	if (
		run->p_width == frame->width
		&& run->p_height == frame->height
		&& run->p_input_format == frame->format
		&& run->p_stride == frame->stride
		&& run->p_dma == dma
	) {
		return; // Configured already
	}

	_LOG_INFO("Configuring encoder: DMA=%d ...", dma);

	_LOG_DEBUG("Encoder changes: width=%u->%u, height=%u->%u, input_format=%u->%u, stride=%u->%u, dma=%u->%u",
		run->p_width, frame->width,
		run->p_height, frame->height,
		run->p_input_format, frame->format,
		run->p_stride, frame->stride,
		run->p_dma, dma);

	_m2m_encoder_cleanup(enc);

	run->p_width = frame->width;
	run->p_height = frame->height;
	run->p_input_format = frame->format;
	run->p_stride = frame->stride;
	run->p_dma = dma;

	_LOG_DEBUG("Opening encoder device ...");
	if ((run->fd = open(enc->path, O_RDWR)) < 0) {
		_LOG_PERROR("Can't open encoder device");
		goto error;
	}
	_LOG_DEBUG("Encoder device fd=%d opened", run->fd);

#	define SET_OPTION(x_cid, x_value) { \
			struct v4l2_control m_ctl = {0}; \
			m_ctl.id = x_cid; \
			m_ctl.value = x_value; \
			_LOG_DEBUG("Configuring option " #x_cid " ..."); \
			_E_XIOCTL(VIDIOC_S_CTRL, &m_ctl, "Can't set option " #x_cid); \
		}
	if (enc->output_format == V4L2_PIX_FMT_H264) {
		SET_OPTION(V4L2_CID_MPEG_VIDEO_BITRATE,				enc->bitrate);
		SET_OPTION(V4L2_CID_MPEG_VIDEO_H264_I_PERIOD,		enc->gop);
		SET_OPTION(V4L2_CID_MPEG_VIDEO_H264_PROFILE,		V4L2_MPEG_VIDEO_H264_PROFILE_CONSTRAINED_BASELINE);
		if (run->p_width * run->p_height <= 1920 * 1080) { // https://forums.raspberrypi.com/viewtopic.php?t=291447#p1762296
			SET_OPTION(V4L2_CID_MPEG_VIDEO_H264_LEVEL,		V4L2_MPEG_VIDEO_H264_LEVEL_4_0);
		} else {
			SET_OPTION(V4L2_CID_MPEG_VIDEO_H264_LEVEL,		V4L2_MPEG_VIDEO_H264_LEVEL_5_1);
		}
		SET_OPTION(V4L2_CID_MPEG_VIDEO_REPEAT_SEQ_HEADER,	1);
		SET_OPTION(V4L2_CID_MPEG_VIDEO_H264_MIN_QP,			16);
		SET_OPTION(V4L2_CID_MPEG_VIDEO_H264_MAX_QP,			32);
	} else if (enc->output_format == V4L2_PIX_FMT_MJPEG) {
		SET_OPTION(V4L2_CID_MPEG_VIDEO_BITRATE,				enc->bitrate);
	} else if (enc->output_format == V4L2_PIX_FMT_JPEG) {
		SET_OPTION(V4L2_CID_JPEG_COMPRESSION_QUALITY,		enc->quality);
	}
#	undef SET_OPTION

	{
		struct v4l2_format fmt = {0};
		fmt.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
		fmt.fmt.pix_mp.width = run->p_width;
		fmt.fmt.pix_mp.height = run->p_height;
		fmt.fmt.pix_mp.pixelformat = run->p_input_format;
		fmt.fmt.pix_mp.field = V4L2_FIELD_ANY;
		fmt.fmt.pix_mp.colorspace = V4L2_COLORSPACE_JPEG; // FIXME: Wrong colors
		fmt.fmt.pix_mp.num_planes = 1;
		// fmt.fmt.pix_mp.plane_fmt[0].bytesperline = run->p_stride;
		_LOG_DEBUG("Configuring INPUT format ...");
		_E_XIOCTL(VIDIOC_S_FMT, &fmt, "Can't set INPUT format");
	}

	{
		struct v4l2_format fmt = {0};
		fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
		fmt.fmt.pix_mp.width = run->p_width;
		fmt.fmt.pix_mp.height = run->p_height;
		fmt.fmt.pix_mp.pixelformat = enc->output_format;
		fmt.fmt.pix_mp.field = V4L2_FIELD_ANY;
		fmt.fmt.pix_mp.colorspace = V4L2_COLORSPACE_DEFAULT;
		fmt.fmt.pix_mp.num_planes = 1;
		// fmt.fmt.pix_mp.plane_fmt[0].bytesperline = 0;
		if (enc->output_format == V4L2_PIX_FMT_H264) {
			// https://github.com/pikvm/ustreamer/issues/169
			// https://github.com/raspberrypi/linux/pull/5232
			fmt.fmt.pix_mp.plane_fmt[0].sizeimage = (1024 + 512) << 10; // 1.5Mb
		}
		_LOG_DEBUG("Configuring OUTPUT format ...");
		_E_XIOCTL(VIDIOC_S_FMT, &fmt, "Can't set OUTPUT format");
		if (fmt.fmt.pix_mp.pixelformat != enc->output_format) {
			char fourcc_str[8];
			_LOG_ERROR("The OUTPUT format can't be configured as %s",
				us_fourcc_to_string(enc->output_format, fourcc_str, 8));
			_LOG_ERROR("In case of Raspberry Pi, try to append 'start_x=1' to /boot/config.txt");
			goto error;
		}
	}

	if (run->p_width * run->p_height <= 1280 * 720) {
		// H264 требует каких-то лимитов. Больше 30 не поддерживается, а при 0
		// через какое-то время начинает производить некорректные фреймы.
		// Если же привысить fps, то резко увеличивается время кодирования.
		run->fps_limit = 60;
	} else {
		run->fps_limit = 30;
	}
	// H264: 30 or 0? https://github.com/6by9/yavta/blob/master/yavta.c#L2100
	// По логике вещей правильно 0, но почему-то на низких разрешениях типа 640x480
	// енкодер через несколько секунд перестает производить корректные фреймы.
	// JPEG: То же самое про 30 or 0, но еще даже не проверено на низких разрешениях.
	{
		struct v4l2_streamparm setfps = {0};
		setfps.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
		setfps.parm.output.timeperframe.numerator = 1;
		setfps.parm.output.timeperframe.denominator = run->fps_limit;
		_LOG_DEBUG("Configuring INPUT FPS ...");
		_E_XIOCTL(VIDIOC_S_PARM, &setfps, "Can't set INPUT FPS");
	}

	if (_m2m_encoder_init_buffers(
		enc, (dma ? "INPUT-DMA" : "INPUT"), V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE,
		&run->input_bufs, &run->n_input_bufs, dma
	) < 0) {
		goto error;
	}
	if (_m2m_encoder_init_buffers(
		enc, "OUTPUT", V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE,
		&run->output_bufs, &run->n_output_bufs, false
	) < 0) {
		goto error;
	}

	{
		enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
		_LOG_DEBUG("Starting INPUT ...");
		_E_XIOCTL(VIDIOC_STREAMON, &type, "Can't start INPUT");

		type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
		_LOG_DEBUG("Starting OUTPUT ...");
		_E_XIOCTL(VIDIOC_STREAMON, &type, "Can't start OUTPUT");
	}

	run->ready = true;
	_LOG_INFO("Encoder is ready");
	return;

error:
	_m2m_encoder_cleanup(enc);
	_LOG_ERROR("Encoder destroyed due an error (prepare)");
}

static int _m2m_encoder_init_buffers(
	us_m2m_encoder_s *enc, const char *name, enum v4l2_buf_type type,
	us_m2m_buffer_s **bufs_ptr, uint *n_bufs_ptr, bool dma) {

	us_m2m_encoder_runtime_s *const run = enc->run;

	_LOG_DEBUG("Initializing %s buffers ...", name);

	struct v4l2_requestbuffers req = {0};
	req.count = 1;
	req.type = type;
	req.memory = (dma ? V4L2_MEMORY_DMABUF : V4L2_MEMORY_MMAP);

	_LOG_DEBUG("Requesting %u %s buffers ...", req.count, name);
	_E_XIOCTL(VIDIOC_REQBUFS, &req, "Can't request %s buffers", name);
	if (req.count < 1) {
		_LOG_ERROR("Insufficient %s buffer memory: %u", name, req.count);
		goto error;
	}
	_LOG_DEBUG("Got %u %s buffers", req.count, name);

	if (dma) {
		*n_bufs_ptr = req.count;
		return 0;
	}

	US_CALLOC(*bufs_ptr, req.count);
	for (*n_bufs_ptr = 0; *n_bufs_ptr < req.count; ++(*n_bufs_ptr)) {
		struct v4l2_buffer buf = {0};
		struct v4l2_plane plane = {0};
		buf.type = type;
		buf.memory = V4L2_MEMORY_MMAP;
		buf.index = *n_bufs_ptr;
		buf.length = 1;
		buf.m.planes = &plane;

		_LOG_DEBUG("Querying %s buffer=%u ...", name, *n_bufs_ptr);
		_E_XIOCTL(VIDIOC_QUERYBUF, &buf, "Can't query %s buffer=%u", name, *n_bufs_ptr);

		_LOG_DEBUG("Mapping %s buffer=%u ...", name, *n_bufs_ptr);
		if (((*bufs_ptr)[*n_bufs_ptr].data = mmap(
			NULL, plane.length,
			PROT_READ | PROT_WRITE, MAP_SHARED,
			run->fd, plane.m.mem_offset
		)) == MAP_FAILED) {
			_LOG_PERROR("Can't map %s buffer=%u", name, *n_bufs_ptr);
			goto error;
		}
		assert((*bufs_ptr)[*n_bufs_ptr].data != NULL);
		(*bufs_ptr)[*n_bufs_ptr].allocated = plane.length;

		_LOG_DEBUG("Queuing %s buffer=%u ...", name, *n_bufs_ptr);
		_E_XIOCTL(VIDIOC_QBUF, &buf, "Can't queue %s buffer=%u", name, *n_bufs_ptr);
	}
	_LOG_DEBUG("All %s buffers are ready", name);
	return 0;

error: // Mostly for _E_XIOCTL
	return -1;
}

static void _m2m_encoder_cleanup(us_m2m_encoder_s *enc) {
	us_m2m_encoder_runtime_s *const run = enc->run;

	bool say = false;

	if (run->ready) {
		say = true;
#		define STOP_STREAM(x_name, x_type) { \
				enum v4l2_buf_type m_type_var = x_type; \
				_LOG_DEBUG("Stopping %s ...", x_name); \
				if (us_xioctl(run->fd, VIDIOC_STREAMOFF, &m_type_var) < 0) { \
					_LOG_PERROR("Can't stop %s", x_name); \
				} \
			}
		STOP_STREAM("OUTPUT", V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE);
		STOP_STREAM("INPUT", V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE);
#		undef STOP_STREAM
	}

#	define DELETE_BUFFERS(x_name, x_target) { \
		if (run->x_target##_bufs != NULL) { \
			say = true; \
			for (uint m_index = 0; m_index < run->n_##x_target##_bufs; ++m_index) { \
				us_m2m_buffer_s *m_buf = &run->x_target##_bufs[m_index]; \
				if (m_buf->allocated > 0 && m_buf->data != NULL) { \
					if (munmap(m_buf->data, m_buf->allocated) < 0) { \
						_LOG_PERROR("Can't unmap %s buffer=%u", #x_name, m_index); \
					} \
				} \
			} \
			US_DELETE(run->x_target##_bufs, free); \
		} \
		run->n_##x_target##_bufs = 0; \
	}
	DELETE_BUFFERS("OUTPUT", output);
	DELETE_BUFFERS("INPUT", input);
#	undef DELETE_BUFFERS

	if (run->fd >= 0) {
		say = true;
		if (close(run->fd) < 0) {
			_LOG_PERROR("Can't close encoder device");
		}
		run->fd = -1;
	}

	run->last_online = -1;
	run->ready = false;

	if (say) {
		_LOG_INFO("Encoder closed");
	}
}

static int _m2m_encoder_compress_raw(us_m2m_encoder_s *enc, const us_frame_s *src, us_frame_s *dest, bool force_key) {
	us_m2m_encoder_runtime_s *const run = enc->run;

	assert(run->ready);

	if (force_key) {
		struct v4l2_control ctl = {0};
		ctl.id = V4L2_CID_MPEG_VIDEO_FORCE_KEY_FRAME;
		ctl.value = 1;
		_LOG_DEBUG("Forcing keyframe ...")
		_E_XIOCTL(VIDIOC_S_CTRL, &ctl, "Can't force keyframe");
	}

	struct v4l2_buffer input_buf = {0};
	struct v4l2_plane input_plane = {0};
	input_buf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	input_buf.length = 1;
	input_buf.m.planes = &input_plane;

	if (run->p_dma) {
		input_buf.index = 0;
		input_buf.memory = V4L2_MEMORY_DMABUF;
		input_buf.field = V4L2_FIELD_NONE;
		input_plane.m.fd = src->dma_fd;
		_LOG_DEBUG("Using INPUT-DMA buffer=%u", input_buf.index);
	} else {
		input_buf.memory = V4L2_MEMORY_MMAP;
		_LOG_DEBUG("Grabbing INPUT buffer ...");
		_E_XIOCTL(VIDIOC_DQBUF, &input_buf, "Can't grab INPUT buffer");
		if (input_buf.index >= run->n_input_bufs) {
			_LOG_ERROR("V4L2 error: grabbed invalid INPUT: buffer=%u, n_bufs=%u",
				input_buf.index, run->n_input_bufs);
			goto error;
		}
		_LOG_DEBUG("Grabbed INPUT buffer=%u", input_buf.index);
	}

	const u64 now_ts = us_get_now_monotonic_u64();
	struct timeval ts = {
		.tv_sec = now_ts / 1000000,
		.tv_usec = now_ts % 1000000,
	};

	input_buf.timestamp.tv_sec = ts.tv_sec;
	input_buf.timestamp.tv_usec = ts.tv_usec;
	input_plane.bytesused = src->used;
	input_plane.length = src->used;
	if (!run->p_dma) {
		memcpy(run->input_bufs[input_buf.index].data, src->data, src->used);
	}

	const char *input_name = (run->p_dma ? "INPUT-DMA" : "INPUT");

	_LOG_DEBUG("Sending%s %s buffer ...", (!run->p_dma ? " (releasing)" : ""), input_name);
	_E_XIOCTL(VIDIOC_QBUF, &input_buf, "Can't send %s buffer", input_name);

	// Для не-DMA отправка буфера по факту являтся освобождением этого буфера
	bool input_released = !run->p_dma;

	// https://github.com/pikvm/ustreamer/issues/253
	// За секунду точно должно закодироваться.
	const ldf deadline_ts = us_get_now_monotonic() + 1;

	while (true) {
		if (us_get_now_monotonic() > deadline_ts) {
			_LOG_ERROR("Waiting for the encoder is too long");
			goto error;
		}

		struct pollfd enc_poll = {run->fd, POLLIN, 0};
		_LOG_DEBUG("Polling encoder ...");
		if (poll(&enc_poll, 1, 1000) < 0 && errno != EINTR) {
			_LOG_PERROR("Can't poll encoder");
			goto error;
		}

		if (enc_poll.revents & POLLIN) {
			if (!input_released) {
				_LOG_DEBUG("Releasing %s buffer=%u ...", input_name, input_buf.index);
				_E_XIOCTL(VIDIOC_DQBUF, &input_buf, "Can't release %s buffer=%u",
					input_name, input_buf.index);
				input_released = true;
			}

			struct v4l2_buffer output_buf = {0};
			struct v4l2_plane output_plane = {0};
			output_buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
			output_buf.memory = V4L2_MEMORY_MMAP;
			output_buf.length = 1;
			output_buf.m.planes = &output_plane;
			_LOG_DEBUG("Fetching OUTPUT buffer ...");
			_E_XIOCTL(VIDIOC_DQBUF, &output_buf, "Can't fetch OUTPUT buffer");

			bool done = false;
			if (ts.tv_sec != output_buf.timestamp.tv_sec || ts.tv_usec != output_buf.timestamp.tv_usec) {
				// Енкодер первый раз может выдать буфер с мусором и нулевым таймстампом,
				// так что нужно убедиться, что мы читаем выходной буфер, соответствующий
				// входному (с тем же таймстампом).
				_LOG_DEBUG("Need to retry OUTPUT buffer due timestamp mismatch");
			} else {
				us_frame_set_data(dest, run->output_bufs[output_buf.index].data, output_plane.bytesused);
				dest->key = output_buf.flags & V4L2_BUF_FLAG_KEYFRAME;
				dest->gop = enc->gop;
				done = true;
			}

			_LOG_DEBUG("Releasing OUTPUT buffer=%u ...", output_buf.index);
			_E_XIOCTL(VIDIOC_QBUF, &output_buf, "Can't release OUTPUT buffer=%u", output_buf.index);

			if (done) {
				break;
			}
		}
	}
	return 0;

error: // Mostly for _E_XIOCTL
	return -1;
}

#undef _E_XIOCTL
