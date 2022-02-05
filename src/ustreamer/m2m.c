/*****************************************************************************
#                                                                            #
#    uStreamer - Lightweight and fast MJPEG-HTTP streamer.                   #
#                                                                            #
#    Copyright (C) 2018-2022  Maxim Devaev <mdevaev@gmail.com>               #
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


static m2m_encoder_s *_m2m_encoder_init(
	const char *name, const char *path, unsigned output_format,
	unsigned fps, bool allow_dma, m2m_option_s *options);

static int _m2m_encoder_prepare(m2m_encoder_s *enc, const frame_s *frame);

static int _m2m_encoder_init_buffers(
	m2m_encoder_s *enc, const char *name, enum v4l2_buf_type type,
	m2m_buffer_s **bufs_ptr, unsigned *n_bufs_ptr, bool dma);

static void _m2m_encoder_cleanup(m2m_encoder_s *enc);

static int _m2m_encoder_compress_raw(m2m_encoder_s *enc, const frame_s *src, frame_s *dest, bool force_key);


#define E_LOG_ERROR(_msg, ...)		LOG_ERROR("%s: " _msg, enc->name, ##__VA_ARGS__)
#define E_LOG_PERROR(_msg, ...)		LOG_PERROR("%s: " _msg, enc->name, ##__VA_ARGS__)
#define E_LOG_INFO(_msg, ...)		LOG_INFO("%s: " _msg, enc->name, ##__VA_ARGS__)
#define E_LOG_VERBOSE(_msg, ...)	LOG_VERBOSE("%s: " _msg, enc->name, ##__VA_ARGS__)
#define E_LOG_DEBUG(_msg, ...)		LOG_DEBUG("%s: " _msg, enc->name, ##__VA_ARGS__)


m2m_encoder_s *m2m_h264_encoder_init(const char *name, const char *path, unsigned bitrate, unsigned gop) {
#	define OPTION(_required, _key, _value) {#_key, _required, V4L2_CID_MPEG_VIDEO_##_key, _value}

	m2m_option_s options[] = {
		OPTION(true, BITRATE, bitrate * 1000),
		// OPTION(false, BITRATE_PEAK, bitrate * 1000),
		OPTION(true, H264_I_PERIOD, gop),
		OPTION(true, H264_PROFILE, V4L2_MPEG_VIDEO_H264_PROFILE_CONSTRAINED_BASELINE),
		OPTION(true, H264_LEVEL, V4L2_MPEG_VIDEO_H264_LEVEL_4_0),
		OPTION(true, REPEAT_SEQ_HEADER, 1),
		OPTION(false, H264_MIN_QP, 16),
		OPTION(false, H264_MAX_QP, 32),
		{NULL, false, 0, 0},
	};

#	undef OPTION

	// FIXME: 30 or 0? https://github.com/6by9/yavta/blob/master/yavta.c#L2100
	// По логике вещей правильно 0, но почему-то на низких разрешениях типа 640x480
	// енкодер через несколько секунд перестает производить корректные фреймы.
	return _m2m_encoder_init(name, path, V4L2_PIX_FMT_H264, 30, true, options);
}

m2m_encoder_s *m2m_mjpeg_encoder_init(const char *name, const char *path, unsigned quality) {
	const double b_min = 25;
	const double b_max = 25000;
	const double step = 25;
	double bitrate = log10(quality) * (b_max - b_min) / 2 + b_min;
	bitrate = step * round(bitrate / step);
	bitrate *= 1000; // From Kbps
	assert(bitrate > 0);

	m2m_option_s options[] = {
		{"BITRATE", true, V4L2_CID_MPEG_VIDEO_BITRATE, bitrate},
		{NULL, false, 0, 0},
	};

	// FIXME: То же самое про 30 or 0, но еще даже не проверено на низких разрешениях
	return _m2m_encoder_init(name, path, V4L2_PIX_FMT_MJPEG, 30, true, options);
}

m2m_encoder_s *m2m_jpeg_encoder_init(const char *name, const char *path, unsigned quality) {
	m2m_option_s options[] = {
		{"QUALITY", true, V4L2_CID_JPEG_COMPRESSION_QUALITY, quality},
		{NULL, false, 0, 0},
	};

	// FIXME: DMA не работает
	return _m2m_encoder_init(name, path, V4L2_PIX_FMT_JPEG, 30, false, options);
}

void m2m_encoder_destroy(m2m_encoder_s *enc) {
	E_LOG_INFO("Destroying encoder ...");
	_m2m_encoder_cleanup(enc);
	free(enc->options);
	free(enc->path);
	free(enc->name);
	free(enc);
}

#define RUN(_next) enc->run->_next

int m2m_encoder_compress(m2m_encoder_s *enc, const frame_s *src, frame_s *dest, bool force_key) {
	frame_encoding_begin(src, dest, (enc->output_format == V4L2_PIX_FMT_MJPEG ? V4L2_PIX_FMT_JPEG : enc->output_format));

	if (
		RUN(width) != src->width
		|| RUN(height) != src->height
		|| RUN(input_format) != src->format
		|| RUN(stride) != src->stride
		|| RUN(dma) != (enc->allow_dma && src->dma_fd >= 0)
	) {
		if (_m2m_encoder_prepare(enc, src) < 0) {
			return -1;
		}
	}

	force_key = (enc->output_format == V4L2_PIX_FMT_H264 && (force_key || RUN(last_online) != src->online));

	if (_m2m_encoder_compress_raw(enc, src, dest, force_key) < 0) {
		_m2m_encoder_cleanup(enc);
		E_LOG_ERROR("Encoder destroyed due an error (compress)");
		return -1;
	}

	frame_encoding_end(dest);

	E_LOG_VERBOSE("Compressed new frame: size=%zu, time=%0.3Lf, force_key=%d",
		dest->used, dest->encode_end_ts - dest->encode_begin_ts, force_key);

	RUN(last_online) = src->online;
	return 0;
}

static m2m_encoder_s *_m2m_encoder_init(
	const char *name, const char *path, unsigned output_format,
	unsigned fps, bool allow_dma, m2m_option_s *options) {

	LOG_INFO("%s: Initializing encoder ...", name);

	m2m_encoder_runtime_s *run;
	A_CALLOC(run, 1);
	run->last_online = -1;
	run->fd = -1;

	m2m_encoder_s *enc;
	A_CALLOC(enc, 1);
	assert(enc->name = strdup(name));
	if (path == NULL) {
		assert(enc->path = strdup(output_format == V4L2_PIX_FMT_JPEG ? "/dev/video31" : "/dev/video11"));
	} else {
		assert(enc->path = strdup(path));
	}
	enc->output_format = output_format;
	enc->fps = fps;
	enc->allow_dma = allow_dma;
	enc->run = run;

	unsigned count = 0;
	for (; options[count].name != NULL; ++count);
	++count;
	A_CALLOC(enc->options, count);
	memcpy(enc->options, options, sizeof(m2m_option_s) * count);

	return enc;
}

#define E_XIOCTL(_request, _value, _msg, ...) { \
		if (xioctl(RUN(fd), _request, _value) < 0) { \
			E_LOG_PERROR(_msg, ##__VA_ARGS__); \
			goto error; \
		} \
	}

static int _m2m_encoder_prepare(m2m_encoder_s *enc, const frame_s *frame) {
	bool dma = (enc->allow_dma && frame->dma_fd >= 0);

	E_LOG_INFO("Configuring encoder: DMA=%d ...", dma);

	_m2m_encoder_cleanup(enc);

	RUN(width) = frame->width;
	RUN(height) = frame->height;
	RUN(input_format) = frame->format;
	RUN(stride) = frame->stride;
	RUN(dma) = dma;

	if ((RUN(fd) = open(enc->path, O_RDWR)) < 0) {
		E_LOG_PERROR("Can't open encoder device");
		goto error;
	}
	E_LOG_DEBUG("Encoder device fd=%d opened", RUN(fd));

	for (m2m_option_s *option = enc->options; option->name != NULL; ++option) {
		struct v4l2_control ctl = {0};
		ctl.id = option->id;
		ctl.value = option->value;

		E_LOG_DEBUG("Configuring option %s ...", option->name);
		if (option->required) {
			E_XIOCTL(VIDIOC_S_CTRL, &ctl, "Can't set option %s", option->name);
		} else {
			if (xioctl(RUN(fd), VIDIOC_S_CTRL, &ctl) < 0) {
				if (errno == EINVAL) {
					E_LOG_ERROR("Can't set option %s: Unsupported by encoder", option->name);
				} else {
					E_LOG_PERROR("Can't set option %s", option->name);
				}
			}
		}
	}

	{
		struct v4l2_format fmt = {0};
		fmt.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
		fmt.fmt.pix_mp.width = RUN(width);
		fmt.fmt.pix_mp.height = RUN(height);
		fmt.fmt.pix_mp.pixelformat = RUN(input_format);
		fmt.fmt.pix_mp.field = V4L2_FIELD_ANY;
		fmt.fmt.pix_mp.colorspace = V4L2_COLORSPACE_JPEG; // libcamera currently has no means to request the right colour space
		fmt.fmt.pix_mp.num_planes = 1;
		// fmt.fmt.pix_mp.plane_fmt[0].bytesperline = RUN(stride);
		E_LOG_DEBUG("Configuring INPUT format ...");
		E_XIOCTL(VIDIOC_S_FMT, &fmt, "Can't set INPUT format");
	}

	{
		struct v4l2_format fmt = {0};
		fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
		fmt.fmt.pix_mp.width = RUN(width);
		fmt.fmt.pix_mp.height = RUN(height);
		fmt.fmt.pix_mp.pixelformat = enc->output_format;
		fmt.fmt.pix_mp.field = V4L2_FIELD_ANY;
		fmt.fmt.pix_mp.colorspace = V4L2_COLORSPACE_DEFAULT;
		fmt.fmt.pix_mp.num_planes = 1;
		// fmt.fmt.pix_mp.plane_fmt[0].bytesperline = 0;
		// fmt.fmt.pix_mp.plane_fmt[0].sizeimage = 512 << 10;
		E_LOG_DEBUG("Configuring OUTPUT format ...");
		E_XIOCTL(VIDIOC_S_FMT, &fmt, "Can't set OUTPUT format");
		if (fmt.fmt.pix_mp.pixelformat != enc->output_format) {
			char fourcc_str[8];
			E_LOG_ERROR("The OUTPUT format can't be configured as %s",
				fourcc_to_string(enc->output_format, fourcc_str, 8));
			E_LOG_ERROR("In case of Raspberry Pi, try to append 'start_x=1' to /boot/config.txt");
			goto error;
		}
	}

	if (enc->fps > 0) { // TODO: Check this for MJPEG
		struct v4l2_streamparm setfps = {0};
		setfps.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
		setfps.parm.output.timeperframe.numerator = 1;
		setfps.parm.output.timeperframe.denominator = enc->fps;
		E_LOG_DEBUG("Configuring INPUT FPS ...");
		E_XIOCTL(VIDIOC_S_PARM, &setfps, "Can't set INPUT FPS");
	}

	if (_m2m_encoder_init_buffers(enc, (dma ? "INPUT-DMA" : "INPUT"), V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE,
		&RUN(input_bufs), &RUN(n_input_bufs), dma) < 0) {
		goto error;
	}
	if (_m2m_encoder_init_buffers(enc, "OUTPUT", V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE,
		&RUN(output_bufs), &RUN(n_output_bufs), false) < 0) {
		goto error;
	}

	{
		enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
		E_LOG_DEBUG("Starting INPUT ...");
		E_XIOCTL(VIDIOC_STREAMON, &type, "Can't start INPUT");

		type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
		E_LOG_DEBUG("Starting OUTPUT ...");
		E_XIOCTL(VIDIOC_STREAMON, &type, "Can't start OUTPUT");
	}

	RUN(ready) = true;
	E_LOG_DEBUG("Encoder state: *** READY ***");
	return 0;

	error:
		_m2m_encoder_cleanup(enc);
		E_LOG_ERROR("Encoder destroyed due an error (prepare)");
		return -1;
}

static int _m2m_encoder_init_buffers(
	m2m_encoder_s *enc, const char *name, enum v4l2_buf_type type,
	m2m_buffer_s **bufs_ptr, unsigned *n_bufs_ptr, bool dma) {

	E_LOG_DEBUG("Initializing %s buffers ...", name);

	struct v4l2_requestbuffers req = {0};
	req.count = 1;
	req.type = type;
	req.memory = (dma ? V4L2_MEMORY_DMABUF : V4L2_MEMORY_MMAP);

	E_LOG_DEBUG("Requesting %u %s buffers ...", req.count, name);
	E_XIOCTL(VIDIOC_REQBUFS, &req, "Can't request %s buffers", name);
	if (req.count < 1) {
		E_LOG_ERROR("Insufficient %s buffer memory: %u", name, req.count);
		goto error;
	}
	E_LOG_DEBUG("Got %u %s buffers", req.count, name);

	if (dma) {
		*n_bufs_ptr = req.count;
	} else {
		A_CALLOC(*bufs_ptr, req.count);
		for (*n_bufs_ptr = 0; *n_bufs_ptr < req.count; ++(*n_bufs_ptr)) {
			struct v4l2_buffer buf = {0};
			struct v4l2_plane plane = {0};
			buf.type = type;
			buf.memory = V4L2_MEMORY_MMAP;
			buf.index = *n_bufs_ptr;
			buf.length = 1;
			buf.m.planes = &plane;

			E_LOG_DEBUG("Querying %s buffer=%u ...", name, *n_bufs_ptr);
			E_XIOCTL(VIDIOC_QUERYBUF, &buf, "Can't query %s buffer=%u", name, *n_bufs_ptr);

			E_LOG_DEBUG("Mapping %s buffer=%u ...", name, *n_bufs_ptr);
			if (((*bufs_ptr)[*n_bufs_ptr].data = mmap(
				NULL,
				plane.length,
				PROT_READ | PROT_WRITE,
				MAP_SHARED,
				RUN(fd),
				plane.m.mem_offset
			)) == MAP_FAILED) {
				E_LOG_PERROR("Can't map %s buffer=%u", name, *n_bufs_ptr);
				goto error;
			}
			(*bufs_ptr)[*n_bufs_ptr].allocated = plane.length;

			E_LOG_DEBUG("Queuing %s buffer=%u ...", name, *n_bufs_ptr);
			E_XIOCTL(VIDIOC_QBUF, &buf, "Can't queue %s buffer=%u", name, *n_bufs_ptr);
		}
	}

	return 0;
	error:
		return -1;
}

static void _m2m_encoder_cleanup(m2m_encoder_s *enc) {
	if (RUN(ready)) {
#		define STOP_STREAM(_name, _type) { \
				enum v4l2_buf_type _type_var = _type; \
				E_LOG_DEBUG("Stopping %s ...", _name); \
				if (xioctl(RUN(fd), VIDIOC_STREAMOFF, &_type_var) < 0) { \
					E_LOG_PERROR("Can't stop %s", _name); \
				} \
			}

		STOP_STREAM("OUTPUT", V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE);
		STOP_STREAM("INPUT", V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE);

#		undef STOP_STREAM
	}

#	define DESTROY_BUFFERS(_name, _target) { \
		if (RUN(_target##_bufs)) { \
			for (unsigned index = 0; index < RUN(n_##_target##_bufs); ++index) { \
				if (RUN(_target##_bufs[index].allocated) > 0 && RUN(_target##_bufs[index].data) != MAP_FAILED) { \
					if (munmap(RUN(_target##_bufs[index].data), RUN(_target##_bufs[index].allocated)) < 0) { \
						E_LOG_PERROR("Can't unmap %s buffer=%u", #_name, index); \
					} \
				} \
			} \
			free(RUN(_target##_bufs)); \
			RUN(_target##_bufs) = NULL; \
		} \
		RUN(n_##_target##_bufs) = 0; \
	}

	DESTROY_BUFFERS("OUTPUT", output);
	DESTROY_BUFFERS("INPUT", input);

#	undef DESTROY_BUFFERS

	if (RUN(fd) >= 0) {
		if (close(RUN(fd)) < 0) {
			E_LOG_PERROR("Can't close encoder device");
		}
		RUN(fd) = -1;
	}

	RUN(last_online) = -1;
	RUN(ready) = false;

	E_LOG_DEBUG("Encoder state: ~~~ NOT READY ~~~");
}

static int _m2m_encoder_compress_raw(m2m_encoder_s *enc, const frame_s *src, frame_s *dest, bool force_key) {
	assert(RUN(ready));

	E_LOG_DEBUG("Compressing new frame; force_key=%d ...", force_key);

	if (force_key) {
		struct v4l2_control ctl = {0};
		ctl.id = V4L2_CID_MPEG_VIDEO_FORCE_KEY_FRAME;
		ctl.value = 1;
		E_LOG_DEBUG("Forcing keyframe ...")
		E_XIOCTL(VIDIOC_S_CTRL, &ctl, "Can't force keyframe");
	}

	struct v4l2_buffer input_buf = {0};
	struct v4l2_plane input_plane = {0};
	input_buf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	input_buf.length = 1;
	input_buf.m.planes = &input_plane;

	if (RUN(dma)) {
		input_buf.index = 0;
		input_buf.memory = V4L2_MEMORY_DMABUF;
		input_buf.field = V4L2_FIELD_NONE;
		input_plane.m.fd = src->dma_fd;
		E_LOG_DEBUG("Using INPUT-DMA buffer=%u", input_buf.index);
	} else {
		input_buf.memory = V4L2_MEMORY_MMAP;
		E_LOG_DEBUG("Grabbing INPUT buffer ...");
		E_XIOCTL(VIDIOC_DQBUF, &input_buf, "Can't grab INPUT buffer");
		if (input_buf.index >= RUN(n_input_bufs)) {
			E_LOG_ERROR("V4L2 error: grabbed invalid INPUT: buffer=%u, n_bufs=%u",
				input_buf.index, RUN(n_input_bufs));
			goto error;
		}
		E_LOG_DEBUG("Grabbed INPUT buffer=%u", input_buf.index);
	}

	uint64_t now = get_now_monotonic_u64();
	struct timeval ts = {
		.tv_sec = now / 1000000,
		.tv_usec = now % 1000000,
	};

	input_buf.timestamp.tv_sec = ts.tv_sec;
	input_buf.timestamp.tv_usec = ts.tv_usec;
	input_plane.bytesused = src->used;
	input_plane.length = src->used;
	if (!RUN(dma)) {
		memcpy(RUN(input_bufs[input_buf.index].data), src->data, src->used);
	}

	const char *input_name = (RUN(dma) ? "INPUT-DMA" : "INPUT");

	E_LOG_DEBUG("Sending%s %s buffer ...", (!RUN(dma) ? " (releasing)" : ""), input_name);
	E_XIOCTL(VIDIOC_QBUF, &input_buf, "Can't send %s buffer", input_name);

	// Для не-DMA отправка буфера по факту являтся освобождением этого буфера
	bool input_released = !RUN(dma);

	while (true) {
		struct pollfd enc_poll = {RUN(fd), POLLIN, 0};

		E_LOG_DEBUG("Polling encoder ...");
		if (poll(&enc_poll, 1, 1000) < 0 && errno != EINTR) {
			E_LOG_PERROR("Can't poll encoder");
			goto error;
		}

		if (enc_poll.revents & POLLIN) {
			if (!input_released) {
				E_LOG_DEBUG("Releasing %s buffer=%u ...", input_name, input_buf.index);
				E_XIOCTL(VIDIOC_DQBUF, &input_buf, "Can't release %s buffer=%u",
					input_name, input_buf.index);
				input_released = true;
			}

			struct v4l2_buffer output_buf = {0};
			struct v4l2_plane output_plane = {0};
			output_buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
			output_buf.memory = V4L2_MEMORY_MMAP;
			output_buf.length = 1;
			output_buf.m.planes = &output_plane;
			E_LOG_DEBUG("Fetching OUTPUT buffer ...");
			E_XIOCTL(VIDIOC_DQBUF, &output_buf, "Can't fetch OUTPUT buffer");

			bool done = false;
			if (ts.tv_sec != output_buf.timestamp.tv_sec || ts.tv_usec != output_buf.timestamp.tv_usec) {
				// Енкодер первый раз может выдать буфер с мусором и нулевым таймстампом,
				// так что нужно убедиться, что мы читаем выходной буфер, соответствующий
				// входному (с тем же таймстампом).
				E_LOG_DEBUG("Need to retry OUTPUT buffer due timestamp mismatch");
			} else {
				frame_set_data(dest, RUN(output_bufs[output_buf.index].data), output_plane.bytesused);
				dest->key = output_buf.flags & V4L2_BUF_FLAG_KEYFRAME;
				done = true;
			}

			E_LOG_DEBUG("Releasing OUTPUT buffer=%u ...", output_buf.index);
			E_XIOCTL(VIDIOC_QBUF, &output_buf, "Can't release OUTPUT buffer=%u", output_buf.index);

			if (done) {
				break;
			}
		}
	}

	return 0;
	error:
		return -1;
}

#undef E_XIOCTL

#undef RUN

#undef E_LOG_DEBUG
#undef E_LOG_VERBOSE
#undef E_LOG_INFO
#undef E_LOG_PERROR
#undef E_LOG_ERROR
