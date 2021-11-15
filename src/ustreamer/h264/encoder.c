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


#include "encoder.h"


static int _h264_encoder_init_buffers(
	h264_encoder_s *enc, const char *name, enum v4l2_buf_type type,
	h264_buffer_s **bufs_ptr, unsigned *n_bufs_ptr, bool dma);

static void _h264_encoder_cleanup(h264_encoder_s *enc);

static int _h264_encoder_compress_raw(
	h264_encoder_s *enc, const frame_s *src, int src_dma_fd,
	frame_s *dest, bool force_key);


h264_encoder_s *h264_encoder_init(const char *path, unsigned bitrate, unsigned gop, unsigned fps) {
	LOG_INFO("H264: Initializing encoder ...");
	LOG_INFO("H264: Using bitrate: %u Kbps", bitrate);
	LOG_INFO("H264: Using GOP: %u", gop);

	h264_encoder_s *enc;
	A_CALLOC(enc, 1);
	assert(enc->path = strdup(path));
	enc->bitrate = bitrate; // Kbps
	enc->gop = gop; // Interval between keyframes
	enc->fps = fps;
	enc->last_online = -1;
	return enc;
}

void h264_encoder_destroy(h264_encoder_s *enc) {
	LOG_INFO("H264: Destroying encoder ...");
	_h264_encoder_cleanup(enc);
	free(enc->path);
	free(enc);
}

bool h264_encoder_is_prepared_for(h264_encoder_s *enc, const frame_s *frame, bool dma) {
#	define EQ(_field) (enc->_field == frame->_field)
	return (EQ(width) && EQ(height) && EQ(format) && EQ(stride) && (enc->dma == dma));
#	undef EQ
}

#define ENCODER_XIOCTL(_request, _value, _msg, ...) { \
		if (xioctl(enc->fd, _request, _value) < 0) { \
			LOG_PERROR(_msg, ##__VA_ARGS__); \
			goto error; \
		} \
	}

int h264_encoder_prepare(h264_encoder_s *enc, const frame_s *frame, bool dma) {
	LOG_INFO("H264: Configuring encoder: DMA=%d ...", dma);

	_h264_encoder_cleanup(enc);

	enc->width = frame->width;
	enc->height = frame->height;
	enc->format = frame->format;
	enc->stride = frame->stride;
	enc->dma = dma;

	if ((enc->fd = open(enc->path, O_RDWR)) < 0) {
		LOG_PERROR("H264: Can't open encoder device");
		goto error;
	}

	{
#		define SET_OPTION(_cid, _value) { \
				struct v4l2_control _ctl = {0}; \
				_ctl.id = _cid; \
				_ctl.value = _value; \
				LOG_DEBUG("H264: Configuring option %s ...", #_cid); \
				ENCODER_XIOCTL(VIDIOC_S_CTRL, &_ctl, "H264: Can't set option " #_cid); \
			}

		SET_OPTION(V4L2_CID_MPEG_VIDEO_BITRATE, enc->bitrate * 1000);
		SET_OPTION(V4L2_CID_MPEG_VIDEO_H264_I_PERIOD, enc->gop);
		SET_OPTION(V4L2_CID_MPEG_VIDEO_H264_PROFILE, V4L2_MPEG_VIDEO_H264_PROFILE_BASELINE);
		SET_OPTION(V4L2_CID_MPEG_VIDEO_H264_LEVEL, V4L2_MPEG_VIDEO_H264_LEVEL_4_0);
		SET_OPTION(V4L2_CID_MPEG_VIDEO_REPEAT_SEQ_HEADER, 1);

#		undef SET_OPTION
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
		LOG_DEBUG("H264: Configuring INPUT format ...");
		ENCODER_XIOCTL(VIDIOC_S_FMT, &fmt, "H264: Can't set INPUT format");
	}

	{
		struct v4l2_format fmt = {0};
		fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
		fmt.fmt.pix_mp.width = frame->width;
		fmt.fmt.pix_mp.height = frame->height;
		fmt.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_H264;
		fmt.fmt.pix_mp.field = V4L2_FIELD_ANY;
		fmt.fmt.pix_mp.colorspace = V4L2_COLORSPACE_DEFAULT;
		fmt.fmt.pix_mp.num_planes = 1;
		fmt.fmt.pix_mp.plane_fmt[0].bytesperline = 0;
		fmt.fmt.pix_mp.plane_fmt[0].sizeimage = 512 << 10;
		LOG_DEBUG("H264: Configuring OUTPUT format ...");
		ENCODER_XIOCTL(VIDIOC_S_FMT, &fmt, "H264: Can't set OUTPUT format");
	}

	{
		struct v4l2_streamparm setfps = {0};
		setfps.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
		setfps.parm.output.timeperframe.numerator = 1;
		setfps.parm.output.timeperframe.denominator = enc->fps;
		LOG_DEBUG("H264: Configuring INPUT FPS ...");
		ENCODER_XIOCTL(VIDIOC_S_PARM, &setfps, "H264: Can't set INPUT FPS");
	}

	if (_h264_encoder_init_buffers(enc, (dma ? "INPUT-DMA" : "INPUT"), V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE,
		&enc->input_bufs, &enc->n_input_bufs, dma) < 0) {
		goto error;
	}
	if (_h264_encoder_init_buffers(enc, "OUTPUT", V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE,
		&enc->output_bufs, &enc->n_output_bufs, false) < 0) {
		goto error;
	}

	{
		enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
		LOG_DEBUG("H264: Starting INPUT ...");
		ENCODER_XIOCTL(VIDIOC_STREAMON, &type, "H264: Can't start INPUT");

		type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
		LOG_DEBUG("H264: Starting OUTPUT ...");
		ENCODER_XIOCTL(VIDIOC_STREAMON, &type, "H264: Can't start OUTPUT");
	}

	enc->ready = true;
	LOG_DEBUG("H264: Encoder state: *** READY ***");
	return 0;

	error:
		_h264_encoder_cleanup(enc);
		LOG_ERROR("H264: Encoder destroyed due an error (prepare)");
		return -1;
}

static int _h264_encoder_init_buffers(
	h264_encoder_s *enc, const char *name, enum v4l2_buf_type type,
	h264_buffer_s **bufs_ptr, unsigned *n_bufs_ptr, bool dma) {

	LOG_DEBUG("H264: Initializing %s buffers: ...", name);

	struct v4l2_requestbuffers req = {0};
	req.count = 1;
	req.type = type;
	req.memory = (dma ? V4L2_MEMORY_DMABUF : V4L2_MEMORY_MMAP);

	LOG_DEBUG("H264: Requesting %u %s buffers ...", req.count, name);
	ENCODER_XIOCTL(VIDIOC_REQBUFS, &req, "H264: Can't request %s buffers", name);
	if (req.count < 1) {
		LOG_ERROR("H264: Insufficient %s buffer memory: %u", name, req.count);
		goto error;
	}
	LOG_DEBUG("H264: Got %u %s buffers", req.count, name);

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

			LOG_DEBUG("H264: Querying %s buffer index=%u ...", name, *n_bufs_ptr);
			ENCODER_XIOCTL(VIDIOC_QUERYBUF, &buf, "H264: Can't query %s buffer index=%u", name, *n_bufs_ptr);

			LOG_DEBUG("H264: Mapping %s buffer index=%u ...", name, *n_bufs_ptr);
			if (((*bufs_ptr)[*n_bufs_ptr].data = mmap(
				NULL,
				plane.length,
				PROT_READ | PROT_WRITE,
				MAP_SHARED,
				enc->fd,
				plane.m.mem_offset
			)) == MAP_FAILED) {
				LOG_PERROR("H264: Can't map %s buffer index=%u", name, *n_bufs_ptr);
				goto error;
			}
			(*bufs_ptr)[*n_bufs_ptr].allocated = plane.length;

			LOG_DEBUG("H264: Queuing %s buffer index=%u ...", name, *n_bufs_ptr);
			ENCODER_XIOCTL(VIDIOC_QBUF, &buf, "H264: Can't queue %s buffer index=%u", name, *n_bufs_ptr);
		}
	}

	return 0;
	error:
		return -1;
}

static void _h264_encoder_cleanup(h264_encoder_s *enc) {
	if (enc->ready) {
#		define STOP_STREAM(_name, _type) { \
				enum v4l2_buf_type _type_var = _type; \
				LOG_DEBUG("H264: Stopping %s ...", _name); \
				if (xioctl(enc->fd, VIDIOC_STREAMOFF, &_type_var) < 0) { \
					LOG_PERROR("H264: Can't stop %s", _name); \
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
						LOG_PERROR("H264: Can't unmap %s buffer index=%u", #_name, index); \
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
			LOG_PERROR("H264: Can't close encoder device");
		}
		enc->fd = -1;
	}

	enc->last_online = -1;
	enc->ready = false;

	LOG_DEBUG("H264: Encoder state: ~~~ NOT READY ~~~");
}

int h264_encoder_compress(h264_encoder_s *enc, const frame_s *src, int src_dma_fd, frame_s *dest, bool force_key) {
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
	dest->format = V4L2_PIX_FMT_H264;
	dest->stride = 0;

	force_key = (force_key || enc->last_online != src->online);

	if (_h264_encoder_compress_raw(enc, src, src_dma_fd, dest, force_key) < 0) {
		_h264_encoder_cleanup(enc);
		LOG_ERROR("H264: Encoder destroyed due an error (compress)");
		return -1;
	}

	dest->encode_end_ts = get_now_monotonic();
	LOG_VERBOSE("H264: Compressed new frame: size=%zu, time=%0.3Lf, force_key=%d",
		dest->used, dest->encode_end_ts - dest->encode_begin_ts, force_key);

	enc->last_online = src->online;
	return 0;
}

static int _h264_encoder_compress_raw(
	h264_encoder_s *enc, const frame_s *src, int src_dma_fd,
	frame_s *dest, bool force_key) {

	LOG_DEBUG("H264: Compressing new frame; force_key=%d ...", force_key);

	if (force_key) {
		struct v4l2_control ctl = {0};
		ctl.id = V4L2_CID_MPEG_VIDEO_FORCE_KEY_FRAME;
		ctl.value = 1;
		LOG_DEBUG("H264: Forcing keyframe ...")
		ENCODER_XIOCTL(VIDIOC_S_CTRL, &ctl, "H264: Can't force keyframe");
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
		LOG_DEBUG("H264: Using INPUT-DMA buffer index=%u", input_buf.index);
	} else {
		assert(src_dma_fd < 0);
		input_buf.memory = V4L2_MEMORY_MMAP;
		LOG_DEBUG("H264: Grabbing INPUT buffer ...");
		ENCODER_XIOCTL(VIDIOC_DQBUF, &input_buf, "H264: Can't grab INPUT buffer");
		if (input_buf.index >= enc->n_input_bufs) {
			LOG_ERROR("H264: V4L2 error: grabbed invalid INPUT buffer: index=%u, n_bufs=%u",
				input_buf.index, enc->n_input_bufs);
			goto error;
		}
		LOG_DEBUG("H264: Grabbed INPUT buffer index=%u", input_buf.index);
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

	LOG_DEBUG("H264: Sending %s buffer ...", input_name);
	ENCODER_XIOCTL(VIDIOC_QBUF, &input_buf, "H264: Can't send %s buffer", input_name);

	bool input_released = false;
	while (true) {
		struct pollfd enc_poll = {enc->fd, POLLIN, 0};

		if (poll(&enc_poll, 1, 200) < 0 && errno != EINTR) {
			LOG_PERROR("H264: Can't poll encoder");
			goto error;
		}

		if (enc_poll.revents & POLLIN) {
			if (!input_released) {
				LOG_DEBUG("H264: Releasing %s buffer index=%u ...", input_name, input_buf.index);
				ENCODER_XIOCTL(VIDIOC_DQBUF, &input_buf, "H264: Can't release %s buffer index=%u",
					input_name, input_buf.index);
				input_released = true;
			}

			struct v4l2_buffer output_buf = {0};
			struct v4l2_plane output_plane = {0};
			output_buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
			output_buf.memory = V4L2_MEMORY_MMAP;
			output_buf.length = 1;
			output_buf.m.planes = &output_plane;
			LOG_DEBUG("H264: Fetching OUTPUT buffer ...");
			ENCODER_XIOCTL(VIDIOC_DQBUF, &output_buf, "H264: Can't fetch OUTPUT buffer");

			frame_set_data(dest, enc->output_bufs[output_buf.index].data, output_plane.bytesused);
			dest->key = output_buf.flags & V4L2_BUF_FLAG_KEYFRAME;

			LOG_DEBUG("H264: Releasing OUTPUT buffer index=%u ...", output_buf.index);
			ENCODER_XIOCTL(VIDIOC_QBUF, &output_buf, "H264: Can't release OUTPUT buffer index=%u", output_buf.index);
			break;
		}
	}

	return 0;
	error:
		return -1;
}

#undef ENCODER_XIOCTL
