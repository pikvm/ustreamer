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


#include "m2m.h"


static int _m2m_encoder_init_buffers(
	m2m_encoder_s *enc, const char *name, enum v4l2_buf_type type,
	m2m_buffer_s **bufs_ptr, unsigned *n_bufs_ptr, bool dma);

static void _m2m_encoder_cleanup(m2m_encoder_s *enc);

static int _m2m_encoder_compress_raw(
	m2m_encoder_s *enc, const frame_s *src, int src_dma_fd,
	frame_s *dest, bool force_key);


#define E_LOG_ERROR(_msg, ...)		LOG_ERROR("%s: " _msg, enc->name, ##__VA_ARGS__)
#define E_LOG_PERROR(_msg, ...)		LOG_PERROR("%s: " _msg, enc->name, ##__VA_ARGS__)
#define E_LOG_INFO(_msg, ...)		LOG_INFO("%s: " _msg, enc->name, ##__VA_ARGS__)
#define E_LOG_VERBOSE(_msg, ...)	LOG_VERBOSE("%s: " _msg, enc->name, ##__VA_ARGS__)
#define E_LOG_DEBUG(_msg, ...)		LOG_DEBUG("%s: " _msg, enc->name, ##__VA_ARGS__)


m2m_encoder_s *m2m_encoder_init(const char *name, const char *path, unsigned format, unsigned fps, m2m_option_s *options) {
	LOG_INFO("%s: Initializing encoder ...", name);

	m2m_encoder_s *enc;
	A_CALLOC(enc, 1);
	assert(enc->name = strdup(name));
	assert(enc->path = strdup(path));
	enc->output_format = format;
	enc->fps = fps;
	enc->last_online = -1;

	unsigned count = 0;
	for (; options[count].name != NULL; ++count);
	++count;
	A_CALLOC(enc->options, count);
	memcpy(enc->options, options, sizeof(m2m_option_s) * count);

	return enc;
}

void m2m_encoder_destroy(m2m_encoder_s *enc) {
	E_LOG_INFO("Destroying encoder ...");
	_m2m_encoder_cleanup(enc);
	free(enc->options);
	free(enc->path);
	free(enc->name);
	free(enc);
}

bool m2m_encoder_is_prepared_for(m2m_encoder_s *enc, const frame_s *frame, bool dma) {
#	define EQ(_field) (enc->_field == frame->_field)
	return (EQ(width) && EQ(height) && EQ(format) && EQ(stride) && (enc->dma == dma));
#	undef EQ
}

#define E_XIOCTL(_request, _value, _msg, ...) { \
		if (xioctl(enc->fd, _request, _value) < 0) { \
			E_LOG_PERROR(_msg, ##__VA_ARGS__); \
			goto error; \
		} \
	}

int m2m_encoder_prepare(m2m_encoder_s *enc, const frame_s *frame, bool dma) {
	E_LOG_INFO("Configuring encoder: DMA=%d ...", dma);

	_m2m_encoder_cleanup(enc);

	enc->width = frame->width;
	enc->height = frame->height;
	enc->format = frame->format;
	enc->stride = frame->stride;
	enc->dma = dma;

	if ((enc->fd = open(enc->path, O_RDWR)) < 0) {
		E_LOG_PERROR("Can't open encoder device");
		goto error;
	}

	for (m2m_option_s *option = enc->options; option->name != NULL; ++option) {
		struct v4l2_control ctl = {0};
		ctl.id = option->id;
		ctl.value = option->value;

		E_LOG_DEBUG("Configuring option %s ...", option->name);
		if (option->required) {
			E_XIOCTL(VIDIOC_S_CTRL, &ctl, "Can't set option %s", option->name);
		} else {
			if (xioctl(enc->fd, VIDIOC_S_CTRL, &ctl) < 0) {
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
		fmt.fmt.pix_mp.width = frame->width;
		fmt.fmt.pix_mp.height = frame->height;
		fmt.fmt.pix_mp.pixelformat = frame->format;
		fmt.fmt.pix_mp.field = V4L2_FIELD_ANY;
		fmt.fmt.pix_mp.colorspace = V4L2_COLORSPACE_JPEG; // libcamera currently has no means to request the right colour space
		fmt.fmt.pix_mp.num_planes = 1;
		E_LOG_DEBUG("Configuring INPUT format ...");
		E_XIOCTL(VIDIOC_S_FMT, &fmt, "Can't set INPUT format");
	}

	{
		struct v4l2_format fmt = {0};
		fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
		fmt.fmt.pix_mp.width = frame->width;
		fmt.fmt.pix_mp.height = frame->height;
		fmt.fmt.pix_mp.pixelformat = enc->output_format;
		fmt.fmt.pix_mp.field = V4L2_FIELD_ANY;
		fmt.fmt.pix_mp.colorspace = V4L2_COLORSPACE_DEFAULT;
		fmt.fmt.pix_mp.num_planes = 1;
		fmt.fmt.pix_mp.plane_fmt[0].bytesperline = 0;
		fmt.fmt.pix_mp.plane_fmt[0].sizeimage = 512 << 10;
		E_LOG_DEBUG("Configuring OUTPUT format ...");
		E_XIOCTL(VIDIOC_S_FMT, &fmt, "Can't set OUTPUT format");
	}

	{
		struct v4l2_streamparm setfps = {0};
		setfps.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
		setfps.parm.output.timeperframe.numerator = 1;
		setfps.parm.output.timeperframe.denominator = enc->fps;
		E_LOG_DEBUG("Configuring INPUT FPS ...");
		E_XIOCTL(VIDIOC_S_PARM, &setfps, "Can't set INPUT FPS");
	}

	if (_m2m_encoder_init_buffers(enc, (dma ? "INPUT-DMA" : "INPUT"), V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE,
		&enc->input_bufs, &enc->n_input_bufs, dma) < 0) {
		goto error;
	}
	if (_m2m_encoder_init_buffers(enc, "OUTPUT", V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE,
		&enc->output_bufs, &enc->n_output_bufs, false) < 0) {
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

	enc->ready = true;
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

			E_LOG_DEBUG("Querying %s buffer index=%u ...", name, *n_bufs_ptr);
			E_XIOCTL(VIDIOC_QUERYBUF, &buf, "Can't query %s buffer index=%u", name, *n_bufs_ptr);

			E_LOG_DEBUG("Mapping %s buffer index=%u ...", name, *n_bufs_ptr);
			if (((*bufs_ptr)[*n_bufs_ptr].data = mmap(
				NULL,
				plane.length,
				PROT_READ | PROT_WRITE,
				MAP_SHARED,
				enc->fd,
				plane.m.mem_offset
			)) == MAP_FAILED) {
				E_LOG_PERROR("Can't map %s buffer index=%u", name, *n_bufs_ptr);
				goto error;
			}
			(*bufs_ptr)[*n_bufs_ptr].allocated = plane.length;

			E_LOG_DEBUG("Queuing %s buffer index=%u ...", name, *n_bufs_ptr);
			E_XIOCTL(VIDIOC_QBUF, &buf, "Can't queue %s buffer index=%u", name, *n_bufs_ptr);
		}
	}

	return 0;
	error:
		return -1;
}

static void _m2m_encoder_cleanup(m2m_encoder_s *enc) {
	if (enc->ready) {
#		define STOP_STREAM(_name, _type) { \
				enum v4l2_buf_type _type_var = _type; \
				E_LOG_DEBUG("Stopping %s ...", _name); \
				if (xioctl(enc->fd, VIDIOC_STREAMOFF, &_type_var) < 0) { \
					E_LOG_PERROR("Can't stop %s", _name); \
				} \
			}

		STOP_STREAM("OUTPUT", V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE);
		STOP_STREAM("INPUT", V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE);

#		undef STOP_STREAM
	}

#	define DESTROY_BUFFERS(_name, _target) { \
		if (enc->_target##_bufs) { \
			for (unsigned index = 0; index < enc->n_##_target##_bufs; ++index) { \
				if (enc->_target##_bufs[index].allocated > 0 && enc->_target##_bufs[index].data != MAP_FAILED) { \
					if (munmap(enc->_target##_bufs[index].data, enc->_target##_bufs[index].allocated) < 0) { \
						E_LOG_PERROR("Can't unmap %s buffer index=%u", #_name, index); \
					} \
				} \
			} \
			free(enc->_target##_bufs); \
			enc->_target##_bufs = NULL; \
		} \
		enc->n_##_target##_bufs = 0; \
	}

	DESTROY_BUFFERS("OUTPUT", output);
	DESTROY_BUFFERS("INPUT", input);

#	undef DESTROY_BUFFERS

	if (enc->fd >= 0) {
		if (close(enc->fd) < 0) {
			E_LOG_PERROR("Can't close encoder device");
		}
		enc->fd = -1;
	}

	enc->last_online = -1;
	enc->ready = false;

	E_LOG_DEBUG("Encoder state: ~~~ NOT READY ~~~");
}

int m2m_encoder_compress(m2m_encoder_s *enc, const frame_s *src, int src_dma_fd, frame_s *dest, bool force_key) {
	assert(enc->ready);
	assert(src->used > 0);
	assert(enc->width == src->width);
	assert(enc->height == src->height);
	assert(enc->format == src->format);
	assert(enc->stride == src->stride);
	if (enc->dma) {
		assert(src_dma_fd >= 0);
	} else {
		assert(src_dma_fd < 0);
	}

	frame_copy_meta(src, dest);
	dest->encode_begin_ts = get_now_monotonic();
	dest->format = enc->output_format;
	dest->stride = 0;

	force_key = (force_key || enc->last_online != src->online);

	if (_m2m_encoder_compress_raw(enc, src, src_dma_fd, dest, force_key) < 0) {
		_m2m_encoder_cleanup(enc);
		E_LOG_ERROR("Encoder destroyed due an error (compress)");
		return -1;
	}

	dest->encode_end_ts = get_now_monotonic();
	E_LOG_VERBOSE("Compressed new frame: size=%zu, time=%0.3Lf, force_key=%d",
		dest->used, dest->encode_end_ts - dest->encode_begin_ts, force_key);

	enc->last_online = src->online;
	return 0;
}

static int _m2m_encoder_compress_raw(
	m2m_encoder_s *enc, const frame_s *src, int src_dma_fd,
	frame_s *dest, bool force_key) {

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

	if (enc->dma) {
		assert(src_dma_fd >= 0);
		input_buf.index = 0;
		input_buf.memory = V4L2_MEMORY_DMABUF;
		input_buf.field = V4L2_FIELD_NONE;
		input_plane.m.fd = src_dma_fd;
		E_LOG_DEBUG("Using INPUT-DMA buffer index=%u", input_buf.index);
	} else {
		assert(src_dma_fd < 0);
		input_buf.memory = V4L2_MEMORY_MMAP;
		E_LOG_DEBUG("Grabbing INPUT buffer ...");
		E_XIOCTL(VIDIOC_DQBUF, &input_buf, "Can't grab INPUT buffer");
		if (input_buf.index >= enc->n_input_bufs) {
			E_LOG_ERROR("V4L2 error: grabbed invalid INPUT buffer: index=%u, n_bufs=%u",
				input_buf.index, enc->n_input_bufs);
			goto error;
		}
		E_LOG_DEBUG("Grabbed INPUT buffer index=%u", input_buf.index);
	}

	uint64_t now = get_now_monotonic_u64();
    input_buf.timestamp.tv_sec = now / 1000000;
    input_buf.timestamp.tv_usec = now % 1000000;
	input_plane.bytesused = src->used;
	input_plane.length = src->used;
	if (!enc->dma) {
		memcpy(enc->input_bufs[input_buf.index].data, src->data, src->used);
	}

	const char *input_name = (enc->dma ? "INPUT-DMA" : "INPUT");

	E_LOG_DEBUG("Sending%s %s buffer ...", (!enc->dma ? " (releasing)" : ""), input_name);
	E_XIOCTL(VIDIOC_QBUF, &input_buf, "Can't send %s buffer", input_name);

	// Для не-DMA отправка буфера по факту являтся освобождением этого буфера
	bool input_released = !enc->dma;

	while (true) {
		struct pollfd enc_poll = {enc->fd, POLLIN, 0};

		if (poll(&enc_poll, 1, 200) < 0 && errno != EINTR) {
			E_LOG_PERROR("Can't poll encoder");
			goto error;
		}

		if (enc_poll.revents & POLLIN) {
			if (!input_released) {
				E_LOG_DEBUG("Releasing %s buffer index=%u ...", input_name, input_buf.index);
				E_XIOCTL(VIDIOC_DQBUF, &input_buf, "Can't release %s buffer index=%u",
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

			frame_set_data(dest, enc->output_bufs[output_buf.index].data, output_plane.bytesused);
			dest->key = output_buf.flags & V4L2_BUF_FLAG_KEYFRAME;

			E_LOG_DEBUG("Releasing OUTPUT buffer index=%u ...", output_buf.index);
			E_XIOCTL(VIDIOC_QBUF, &output_buf, "Can't release OUTPUT buffer index=%u", output_buf.index);
			break;
		}
	}

	return 0;
	error:
		return -1;
}

#undef E_XIOCTL

#undef E_LOG_DEBUG
#undef E_LOG_VERBOSE
#undef E_LOG_INFO
#undef E_LOG_PERROR
#undef E_LOG_ERROR
