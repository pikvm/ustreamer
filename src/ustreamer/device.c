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


#include "device.h"


static const struct {
	const char *name;
	const v4l2_std_id standard;
} _STANDARDS[] = {
	{"UNKNOWN",	V4L2_STD_UNKNOWN},
	{"PAL",		V4L2_STD_PAL},
	{"NTSC",	V4L2_STD_NTSC},
	{"SECAM",	V4L2_STD_SECAM},
};

static const struct {
	const char *name;
	const unsigned format;
} _FORMATS[] = {
	{"YUYV",	V4L2_PIX_FMT_YUYV},
	{"UYVY",	V4L2_PIX_FMT_UYVY},
	{"RGB565",	V4L2_PIX_FMT_RGB565},
	{"RGB24",	V4L2_PIX_FMT_RGB24},
	{"MJPEG",	V4L2_PIX_FMT_MJPEG},
	{"JPEG",	V4L2_PIX_FMT_JPEG},
};

static const struct {
	const char *name;
	const enum v4l2_memory io_method;
} _IO_METHODS[] = {
	{"MMAP",	V4L2_MEMORY_MMAP},
	{"USERPTR",	V4L2_MEMORY_USERPTR},
};


static int _device_open_check_cap(device_s *dev);
static int _device_open_dv_timings(device_s *dev);
static int _device_apply_dv_timings(device_s *dev);
static int _device_open_format(device_s *dev, bool first);
static void _device_open_hw_fps(device_s *dev);
static void _device_open_jpeg_quality(device_s *dev);
static int _device_open_io_method(device_s *dev);
static int _device_open_io_method_mmap(device_s *dev);
static int _device_open_io_method_userptr(device_s *dev);
static int _device_open_queue_buffers(device_s *dev);
static int _device_apply_resolution(device_s *dev, unsigned width, unsigned height);

static void _device_apply_controls(device_s *dev);
static int _device_query_control(
	device_s *dev, struct v4l2_queryctrl *query,
	const char *name, unsigned cid, bool quiet);
static void _device_set_control(
	device_s *dev, struct v4l2_queryctrl *query,
	const char *name, unsigned cid, int value, bool quiet);

static const char *_format_to_string_nullable(unsigned format);
static const char *_format_to_string_supported(unsigned format);
static const char *_standard_to_string(v4l2_std_id standard);
static const char *_io_method_to_string_supported(enum v4l2_memory io_method);


#define RUN(_next)		dev->run->_next
#define D_XIOCTL(...)	xioctl(RUN(fd), __VA_ARGS__)


device_s *device_init(void) {
	device_runtime_s *run;
	A_CALLOC(run, 1);
	run->fd = -1;

	device_s *dev;
	A_CALLOC(dev, 1);
	dev->path = "/dev/video0";
	dev->width = 640;
	dev->height = 480;
	dev->format = V4L2_PIX_FMT_YUYV;
	dev->jpeg_quality = 80;
	dev->standard = V4L2_STD_UNKNOWN;
	dev->io_method = V4L2_MEMORY_MMAP;
	dev->n_bufs = get_cores_available() + 1;
	dev->min_frame_size = 128;
	dev->timeout = 1;
	dev->run = run;
	return dev;
}

void device_destroy(device_s *dev) {
	free(dev->run);
	free(dev);
}

int device_parse_format(const char *str) {
	for (unsigned index = 0; index < ARRAY_LEN(_FORMATS); ++index) {
		if (!strcasecmp(str, _FORMATS[index].name)) {
			return _FORMATS[index].format;
		}
	}
	return FORMAT_UNKNOWN;
}

v4l2_std_id device_parse_standard(const char *str) {
	for (unsigned index = 1; index < ARRAY_LEN(_STANDARDS); ++index) {
		if (!strcasecmp(str, _STANDARDS[index].name)) {
			return _STANDARDS[index].standard;
		}
	}
	return STANDARD_UNKNOWN;
}

int device_parse_io_method(const char *str) {
	for (unsigned index = 0; index < ARRAY_LEN(_IO_METHODS); ++index) {
		if (!strcasecmp(str, _IO_METHODS[index].name)) {
			return _IO_METHODS[index].io_method;
		}
	}
	return IO_METHOD_UNKNOWN;
}

int device_open(device_s *dev) {
	if ((RUN(fd) = open(dev->path, O_RDWR|O_NONBLOCK)) < 0) {
		LOG_PERROR("Can't open device");
		goto error;
	}
	LOG_INFO("Device fd=%d opened", RUN(fd));

	if (_device_open_check_cap(dev) < 0) {
		goto error;
	}
	if (_device_open_dv_timings(dev) < 0) {
		goto error;
	}
	if (_device_open_format(dev, true) < 0) {
		goto error;
	}
	_device_open_hw_fps(dev);
	_device_open_jpeg_quality(dev);
	if (_device_open_io_method(dev) < 0) {
		goto error;
	}
	if (_device_open_queue_buffers(dev) < 0) {
		goto error;
	}
	_device_apply_controls(dev);

	LOG_DEBUG("Device fd=%d initialized", RUN(fd));
	return 0;

	error:
		device_close(dev);
		return -1;
}

void device_close(device_s *dev) {
	RUN(persistent_timeout_reported) = false;

	if (RUN(hw_bufs)) {
		LOG_DEBUG("Releasing device buffers ...");
		for (unsigned index = 0; index < RUN(n_bufs); ++index) {
#			define HW(_next) RUN(hw_bufs)[index]._next

			if (HW(dma_fd) >= 0) {
				close(HW(dma_fd));
				HW(dma_fd) = -1;
			}

			if (dev->io_method == V4L2_MEMORY_MMAP) {
				if (HW(raw.allocated) > 0 && HW(raw.data) != MAP_FAILED) {
					if (munmap(HW(raw.data), HW(raw.allocated)) < 0) {
						LOG_PERROR("Can't unmap device buffer=%u", index);
					}
				}
			} else { // V4L2_MEMORY_USERPTR
				if (HW(raw.data)) {
					free(HW(raw.data));
				}
			}

#			undef HW
		}
		RUN(n_bufs) = 0;
		free(RUN(hw_bufs));
		RUN(hw_bufs) = NULL;
	}

	if (RUN(fd) >= 0) {
		LOG_DEBUG("Closing device ...");
		if (close(RUN(fd)) < 0) {
			LOG_PERROR("Can't close device fd=%d", RUN(fd));
		} else {
			LOG_INFO("Device fd=%d closed", RUN(fd));
		}
		RUN(fd) = -1;
	}
}

int device_export_to_dma(device_s *dev) {
#	define DMA_FD		RUN(hw_bufs[index].dma_fd)

	for (unsigned index = 0; index < RUN(n_bufs); ++index) {
		struct v4l2_exportbuffer exp = {0};
		exp.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		exp.index = index;

		LOG_DEBUG("Exporting device buffer=%u to DMA ...", index);
		if (D_XIOCTL(VIDIOC_EXPBUF, &exp) < 0) {
			LOG_PERROR("Can't export device buffer=%u to DMA", index);
			goto error;
		}
		DMA_FD = exp.fd;
	}

	return 0;

	error:
		for (unsigned index = 0; index < RUN(n_bufs); ++index) {
			if (DMA_FD >= 0) {
				close(DMA_FD);
				DMA_FD = -1;
			}
		}
		return -1;

#	undef DMA_FD
}

int device_switch_capturing(device_s *dev, bool enable) {
	if (enable != RUN(capturing)) {
		enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

		LOG_DEBUG("%s device capturing ...", (enable ? "Starting" : "Stopping"));
		if (D_XIOCTL((enable ? VIDIOC_STREAMON : VIDIOC_STREAMOFF), &type) < 0) {
			LOG_PERROR("Can't %s capturing", (enable ? "start" : "stop"));
			if (enable) {
				return -1;
			}
		}

		RUN(capturing) = enable;
		LOG_INFO("Capturing %s", (enable ? "started" : "stopped"));
	}
	return 0;
}

int device_select(device_s *dev, bool *has_read, bool *has_write, bool *has_error) {
	int retval;

#	define INIT_FD_SET(_set) \
		fd_set _set; FD_ZERO(&_set); FD_SET(RUN(fd), &_set);

	INIT_FD_SET(read_fds);
	INIT_FD_SET(write_fds);
	INIT_FD_SET(error_fds);

#	undef INIT_FD_SET

	struct timeval timeout;
	timeout.tv_sec = dev->timeout;
	timeout.tv_usec = 0;

	LOG_DEBUG("Calling select() on video device ...");

	retval = select(RUN(fd) + 1, &read_fds, &write_fds, &error_fds, &timeout);
	if (retval > 0) {
		*has_read = FD_ISSET(RUN(fd), &read_fds);
		*has_write = FD_ISSET(RUN(fd), &write_fds);
		*has_error = FD_ISSET(RUN(fd), &error_fds);
	} else {
		*has_read = false;
		*has_write = false;
		*has_error = false;
	}
	LOG_DEBUG("Device select() --> %d", retval);

	if (retval > 0) {
		RUN(persistent_timeout_reported) = false;
	} else if (retval == 0) {
		if (dev->persistent) {
			if (!RUN(persistent_timeout_reported)) {
				LOG_ERROR("Persistent device timeout (unplugged)");
				RUN(persistent_timeout_reported) = true;
			}
		} else {
			// Если устройство не персистентное, то таймаут является ошибкой
			retval = -1;
		}
	}
	return retval;
}

int device_grab_buffer(device_s *dev, hw_buffer_s **hw) {
	*hw = NULL;

	struct v4l2_buffer buf = {0};
	buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	buf.memory = dev->io_method;

	LOG_DEBUG("Grabbing device buffer ...");
	if (D_XIOCTL(VIDIOC_DQBUF, &buf) < 0) {
		LOG_PERROR("Can't grab device buffer");
		return -1;
	}

	LOG_DEBUG("Grabbed new frame: buffer=%u, bytesused=%u", buf.index, buf.bytesused);

	if (buf.index >= RUN(n_bufs)) {
		LOG_ERROR("V4L2 error: grabbed invalid device buffer=%u, n_bufs=%u", buf.index, RUN(n_bufs));
		return -1;
	}

	// Workaround for broken, corrupted frames:
	// Under low light conditions corrupted frames may get captured.
	// The good thing is such frames are quite small compared to the regular frames.
	// For example a VGA (640x480) webcam frame is normally >= 8kByte large,
	// corrupted frames are smaller.
	if (buf.bytesused < dev->min_frame_size) {
		LOG_DEBUG("Dropped too small frame, assuming it was broken: buffer=%u, bytesused=%u",
			buf.index, buf.bytesused);
		LOG_DEBUG("Releasing device buffer=%u (broken frame) ...", buf.index);
		if (D_XIOCTL(VIDIOC_QBUF, &buf) < 0) {
			LOG_PERROR("Can't release device buffer=%u (broken frame)", buf.index);
			return -1;
		}
		return -2;
	}

#	define HW(_next) RUN(hw_bufs)[buf.index]._next

	if (HW(grabbed)) {
		LOG_ERROR("V4L2 error: grabbed device buffer=%u is already used", buf.index);
		return -1;
	}
	HW(grabbed) = true;

	HW(raw.dma_fd) = HW(dma_fd);
	HW(raw.used) = buf.bytesused;
	HW(raw.width) = RUN(width);
	HW(raw.height) = RUN(height);
	HW(raw.format) = RUN(format);
	HW(raw.stride) = RUN(stride);
	HW(raw.online) = true;
	memcpy(&HW(buf), &buf, sizeof(struct v4l2_buffer));
	HW(raw.grab_ts) = get_now_monotonic();

#	undef HW
	*hw = &RUN(hw_bufs[buf.index]);
	return buf.index;
}

int device_release_buffer(device_s *dev, hw_buffer_s *hw) {
	const unsigned index = hw->buf.index;
	LOG_DEBUG("Releasing device buffer=%u ...", index);

	if (D_XIOCTL(VIDIOC_QBUF, &hw->buf) < 0) {
		LOG_PERROR("Can't release device buffer=%u", index);
		return -1;
	}
	hw->grabbed = false;
	return 0;
}

int device_consume_event(device_s *dev) {
	struct v4l2_event event;

	LOG_DEBUG("Consuming V4L2 event ...");
	if (D_XIOCTL(VIDIOC_DQEVENT, &event) == 0) {
		switch (event.type) {
			case V4L2_EVENT_SOURCE_CHANGE:
				LOG_INFO("Got V4L2_EVENT_SOURCE_CHANGE: source changed");
				return -1;
			case V4L2_EVENT_EOS:
				LOG_INFO("Got V4L2_EVENT_EOS: end of stream (ignored)");
				return 0;
		}
	} else {
		LOG_PERROR("Got some V4L2 device event, but where is it? ");
	}
	return 0;
}

static int _device_open_check_cap(device_s *dev) {
	struct v4l2_capability cap = {0};

	LOG_DEBUG("Querying device capabilities ...");
	if (D_XIOCTL(VIDIOC_QUERYCAP, &cap) < 0) {
		LOG_PERROR("Can't query device capabilities");
		return -1;
	}

	if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
		LOG_ERROR("Video capture is not supported by device");
		return -1;
	}

	if (!(cap.capabilities & V4L2_CAP_STREAMING)) {
		LOG_ERROR("Device doesn't support streaming IO");
		return -1;
	}

	int input = dev->input; // Needs a pointer to int for ioctl()
	LOG_INFO("Using input channel: %d", input);
	if (D_XIOCTL(VIDIOC_S_INPUT, &input) < 0) {
		LOG_ERROR("Can't set input channel");
		return -1;
	}

	if (dev->standard != V4L2_STD_UNKNOWN) {
		LOG_INFO("Using TV standard: %s", _standard_to_string(dev->standard));
		if (D_XIOCTL(VIDIOC_S_STD, &dev->standard) < 0) {
			LOG_ERROR("Can't set video standard");
			return -1;
		}
	} else {
		LOG_DEBUG("Using TV standard: DEFAULT");
	}
	return 0;
}

static int _device_open_dv_timings(device_s *dev) {
	_device_apply_resolution(dev, dev->width, dev->height);
	if (dev->dv_timings) {
		LOG_DEBUG("Using DV-timings");

		if (_device_apply_dv_timings(dev) < 0) {
			return -1;
		}

		struct v4l2_event_subscription sub = {0};
		sub.type = V4L2_EVENT_SOURCE_CHANGE;

		LOG_DEBUG("Subscribing to DV-timings events ...")
		if (D_XIOCTL(VIDIOC_SUBSCRIBE_EVENT, &sub) < 0) {
			LOG_PERROR("Can't subscribe to DV-timings events");
			return -1;
		}
	}
	return 0;
}

static int _device_apply_dv_timings(device_s *dev) {
	struct v4l2_dv_timings dv = {0};

	LOG_DEBUG("Calling xioctl(VIDIOC_QUERY_DV_TIMINGS) ...");
	if (D_XIOCTL(VIDIOC_QUERY_DV_TIMINGS, &dv) == 0) {
		if (dv.type == V4L2_DV_BT_656_1120) {
			// See v4l2_print_dv_timings() in the kernel
			unsigned htot = V4L2_DV_BT_FRAME_WIDTH(&dv.bt);
			unsigned vtot = V4L2_DV_BT_FRAME_HEIGHT(&dv.bt);
			if (dv.bt.interlaced) {
				vtot /= 2;
			}
			unsigned fps = ((htot * vtot) > 0 ? ((100 * (uint64_t)dv.bt.pixelclock)) / (htot * vtot) : 0);
			LOG_INFO("Got new DV-timings: %ux%u%s%u.%02u, pixclk=%llu, vsync=%u, hsync=%u",
				dv.bt.width, dv.bt.height, (dv.bt.interlaced ? "i" : "p"), fps / 100, fps % 100,
				(unsigned long long)dv.bt.pixelclock, dv.bt.vsync, dv.bt.hsync); // See #11 about %llu
		} else {
			LOG_INFO("Got new DV-timings: %ux%u, pixclk=%llu, vsync=%u, hsync=%u",
				dv.bt.width, dv.bt.height,
				(unsigned long long)dv.bt.pixelclock, dv.bt.vsync, dv.bt.hsync);
		}

		LOG_DEBUG("Calling xioctl(VIDIOC_S_DV_TIMINGS) ...");
		if (D_XIOCTL(VIDIOC_S_DV_TIMINGS, &dv) < 0) {
			LOG_PERROR("Failed to set DV-timings");
			return -1;
		}

		if (_device_apply_resolution(dev, dv.bt.width, dv.bt.height) < 0) {
			return -1;
		}

	} else {
		LOG_DEBUG("Calling xioctl(VIDIOC_QUERYSTD) ...");
		if (D_XIOCTL(VIDIOC_QUERYSTD, &dev->standard) == 0) {
			LOG_INFO("Applying the new VIDIOC_S_STD: %s ...", _standard_to_string(dev->standard));
			if (D_XIOCTL(VIDIOC_S_STD, &dev->standard) < 0) {
				LOG_PERROR("Can't set video standard");
				return -1;
			}
		}
	}
	return 0;
}

static int _device_open_format(device_s *dev, bool first) {
	const unsigned stride = align_size(RUN(width), 32) << 1;

	struct v4l2_format fmt = {0};
	fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	fmt.fmt.pix.width = RUN(width);
	fmt.fmt.pix.height = RUN(height);
	fmt.fmt.pix.pixelformat = dev->format;
	fmt.fmt.pix.field = V4L2_FIELD_ANY;
	fmt.fmt.pix.bytesperline = stride;

	// Set format
	LOG_DEBUG("Probing device format=%s, stride=%u, resolution=%ux%u ...",
		_format_to_string_supported(dev->format), stride, RUN(width), RUN(height));
	if (D_XIOCTL(VIDIOC_S_FMT, &fmt) < 0) {
		LOG_PERROR("Can't set device format");
		return -1;
	}

	// Check resolution
	bool retry = false;
	if (fmt.fmt.pix.width != RUN(width) || fmt.fmt.pix.height != RUN(height)) {
		LOG_ERROR("Requested resolution=%ux%u is unavailable", RUN(width), RUN(height));
		retry = true;
	}
	if (_device_apply_resolution(dev, fmt.fmt.pix.width, fmt.fmt.pix.height) < 0) {
		return -1;
	}
	if (first && retry) {
		return _device_open_format(dev, false);
	}
	LOG_INFO("Using resolution: %ux%u", RUN(width), RUN(height));

	// Check format
	if (fmt.fmt.pix.pixelformat != dev->format) {
		LOG_ERROR("Could not obtain the requested format=%s; driver gave us %s",
			_format_to_string_supported(dev->format),
			_format_to_string_supported(fmt.fmt.pix.pixelformat));

		char *format_str;
		if ((format_str = (char *)_format_to_string_nullable(fmt.fmt.pix.pixelformat)) != NULL) {
			LOG_INFO("Falling back to format=%s", format_str);
		} else {
			char fourcc_str[8];
			LOG_ERROR("Unsupported format=%s (fourcc)",
				fourcc_to_string(fmt.fmt.pix.pixelformat, fourcc_str, 8));
			return -1;
		}
	}

	RUN(format) = fmt.fmt.pix.pixelformat;
	LOG_INFO("Using format: %s", _format_to_string_supported(RUN(format)));

	RUN(stride) = fmt.fmt.pix.bytesperline;
	RUN(raw_size) = fmt.fmt.pix.sizeimage; // Only for userptr
	return 0;
}

static void _device_open_hw_fps(device_s *dev) {
	RUN(hw_fps) = 0;

	struct v4l2_streamparm setfps = {0};
	setfps.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

	LOG_DEBUG("Querying HW FPS ...");
	if (D_XIOCTL(VIDIOC_G_PARM, &setfps) < 0) {
		if (errno == ENOTTY) { // Quiet message for TC358743
			LOG_INFO("Querying HW FPS changing is not supported");
		} else {
			LOG_PERROR("Can't query HW FPS changing");
		}
		return;
	}

	if (!(setfps.parm.capture.capability & V4L2_CAP_TIMEPERFRAME)) {
		LOG_INFO("Changing HW FPS is not supported");
		return;
	}

#	define SETFPS_TPF(_next) setfps.parm.capture.timeperframe._next

	MEMSET_ZERO(setfps);
	setfps.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	SETFPS_TPF(numerator) = 1;
	SETFPS_TPF(denominator) = (dev->desired_fps == 0 ? 255 : dev->desired_fps);

	if (D_XIOCTL(VIDIOC_S_PARM, &setfps) < 0) {
		LOG_PERROR("Can't set HW FPS");
		return;
	}

	if (SETFPS_TPF(numerator) != 1) {
		LOG_ERROR("Invalid HW FPS numerator: %u != 1", SETFPS_TPF(numerator));
		return;
	}

	if (SETFPS_TPF(denominator) == 0) { // Не знаю, бывает ли так, но пускай на всякий случай
		LOG_ERROR("Invalid HW FPS denominator: 0");
		return;
	}

	RUN(hw_fps) = SETFPS_TPF(denominator);
	if (dev->desired_fps != RUN(hw_fps)) {
		LOG_INFO("Using HW FPS: %u -> %u (coerced)", dev->desired_fps, RUN(hw_fps));
	} else {
		LOG_INFO("Using HW FPS: %u", RUN(hw_fps));
	}

#	undef SETFPS_TPF
}

static void _device_open_jpeg_quality(device_s *dev) {
	unsigned quality = 0;

	if (is_jpeg(RUN(format))) {
		struct v4l2_jpegcompression comp = {0};

		if (D_XIOCTL(VIDIOC_G_JPEGCOMP, &comp) < 0) {
			LOG_ERROR("Device doesn't support setting of HW encoding quality parameters");
		} else {
			comp.quality = dev->jpeg_quality;
			if (D_XIOCTL(VIDIOC_S_JPEGCOMP, &comp) < 0) {
				LOG_ERROR("Can't change MJPEG quality for JPEG source with HW pass-through encoder");
			} else {
				quality = dev->jpeg_quality;
			}
		}
	}

	RUN(jpeg_quality) = quality;
}

static int _device_open_io_method(device_s *dev) {
	LOG_INFO("Using IO method: %s", _io_method_to_string_supported(dev->io_method));
	switch (dev->io_method) {
		case V4L2_MEMORY_MMAP: return _device_open_io_method_mmap(dev);
		case V4L2_MEMORY_USERPTR: return _device_open_io_method_userptr(dev);
		default: assert(0 && "Unsupported IO method");
	}
	return -1;
}

static int _device_open_io_method_mmap(device_s *dev) {
	struct v4l2_requestbuffers req = {0};
	req.count = dev->n_bufs;
	req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	req.memory = V4L2_MEMORY_MMAP;

	LOG_DEBUG("Requesting %u device buffers for MMAP ...", req.count);
	if (D_XIOCTL(VIDIOC_REQBUFS, &req) < 0) {
		LOG_PERROR("Device '%s' doesn't support MMAP method", dev->path);
		return -1;
	}

	if (req.count < 1) {
		LOG_ERROR("Insufficient buffer memory: %u", req.count);
		return -1;
	} else {
		LOG_INFO("Requested %u device buffers, got %u", dev->n_bufs, req.count);
	}

	LOG_DEBUG("Allocating device buffers ...");

	A_CALLOC(RUN(hw_bufs), req.count);
	for (RUN(n_bufs) = 0; RUN(n_bufs) < req.count; ++RUN(n_bufs)) {
		struct v4l2_buffer buf = {0};
		buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buf.memory = V4L2_MEMORY_MMAP;
		buf.index = RUN(n_bufs);

		LOG_DEBUG("Calling xioctl(VIDIOC_QUERYBUF) for device buffer=%u ...", RUN(n_bufs));
		if (D_XIOCTL(VIDIOC_QUERYBUF, &buf) < 0) {
			LOG_PERROR("Can't VIDIOC_QUERYBUF");
			return -1;
		}

#		define HW(_next) RUN(hw_bufs)[RUN(n_bufs)]._next

		HW(dma_fd) = -1;

		LOG_DEBUG("Mapping device buffer=%u ...", RUN(n_bufs));
		if ((HW(raw.data) = mmap(
			NULL,
			buf.length,
			PROT_READ | PROT_WRITE,
			MAP_SHARED,
			RUN(fd),
			buf.m.offset
		)) == MAP_FAILED) {
			LOG_PERROR("Can't map device buffer=%u", RUN(n_bufs));
			return -1;
		}
		HW(raw.allocated) = buf.length;

#		undef HW
	}
	return 0;
}

static int _device_open_io_method_userptr(device_s *dev) {
	struct v4l2_requestbuffers req = {0};
	req.count = dev->n_bufs;
	req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	req.memory = V4L2_MEMORY_USERPTR;

	LOG_DEBUG("Requesting %u device buffers for USERPTR ...", req.count);
	if (D_XIOCTL(VIDIOC_REQBUFS, &req) < 0) {
		LOG_PERROR("Device '%s' doesn't support USERPTR method", dev->path);
		return -1;
	}

	if (req.count < 1) {
		LOG_ERROR("Insufficient buffer memory: %u", req.count);
		return -1;
	} else {
		LOG_INFO("Requested %u device buffers, got %u", dev->n_bufs, req.count);
	}

	LOG_DEBUG("Allocating device buffers ...");

	A_CALLOC(RUN(hw_bufs), req.count);

	const unsigned page_size = getpagesize();
	const unsigned buf_size = align_size(RUN(raw_size), page_size);

	for (RUN(n_bufs) = 0; RUN(n_bufs) < req.count; ++RUN(n_bufs)) {
#       define HW(_next) RUN(hw_bufs)[RUN(n_bufs)]._next
		assert(HW(raw.data) = aligned_alloc(page_size, buf_size));
		memset(HW(raw.data), 0, buf_size);
		HW(raw.allocated) = buf_size;
#		undef HW
	}
	return 0;
}

static int _device_open_queue_buffers(device_s *dev) {
	for (unsigned index = 0; index < RUN(n_bufs); ++index) {
		struct v4l2_buffer buf = {0};
		buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buf.memory = dev->io_method;
		buf.index = index;
		if (dev->io_method == V4L2_MEMORY_USERPTR) {
			buf.m.userptr = (unsigned long)RUN(hw_bufs)[index].raw.data;
			buf.length = RUN(hw_bufs)[index].raw.allocated;
		}

		LOG_DEBUG("Calling xioctl(VIDIOC_QBUF) for buffer=%u ...", index);
		if (D_XIOCTL(VIDIOC_QBUF, &buf) < 0) {
			LOG_PERROR("Can't VIDIOC_QBUF");
			return -1;
		}
	}
	return 0;
}

static int _device_apply_resolution(device_s *dev, unsigned width, unsigned height) {
	// Тут VIDEO_MIN_* не используются из-за странностей минимального разрешения при отсутствии сигнала
	// у некоторых устройств, например TC358743
	if (
		width == 0 || width > VIDEO_MAX_WIDTH
		|| height == 0 || height > VIDEO_MAX_HEIGHT
	) {
		LOG_ERROR("Requested forbidden resolution=%ux%u: min=1x1, max=%ux%u",
			width, height, VIDEO_MAX_WIDTH, VIDEO_MAX_HEIGHT);
		return -1;
	}
	RUN(width) = width;
	RUN(height) = height;
	return 0;
}

static void _device_apply_controls(device_s *dev) {
#	define SET_CID_VALUE(_cid, _field, _value, _quiet) { \
			struct v4l2_queryctrl query; \
			if (_device_query_control(dev, &query, #_field, _cid, _quiet) == 0) { \
				_device_set_control(dev, &query, #_field, _cid, _value, _quiet); \
			} \
		}

#	define SET_CID_DEFAULT(_cid, _field, _quiet) { \
			struct v4l2_queryctrl query; \
			if (_device_query_control(dev, &query, #_field, _cid, _quiet) == 0) { \
				_device_set_control(dev, &query, #_field, _cid, query.default_value, _quiet); \
			} \
		}

#	define CONTROL_MANUAL_CID(_cid, _field) { \
			if (dev->ctl._field.mode == CTL_MODE_VALUE) { \
				SET_CID_VALUE(_cid, _field, dev->ctl._field.value, false); \
			} else if (dev->ctl._field.mode == CTL_MODE_DEFAULT) { \
				SET_CID_DEFAULT(_cid, _field, false); \
			} \
		}

#	define CONTROL_AUTO_CID(_cid_auto, _cid_manual, _field) { \
			if (dev->ctl._field.mode == CTL_MODE_VALUE) { \
				SET_CID_VALUE(_cid_auto, _field##_auto, 0, true); \
				SET_CID_VALUE(_cid_manual, _field, dev->ctl._field.value, false); \
			} else if (dev->ctl._field.mode == CTL_MODE_AUTO) { \
				SET_CID_VALUE(_cid_auto, _field##_auto, 1, false); \
			} else if (dev->ctl._field.mode == CTL_MODE_DEFAULT) { \
				SET_CID_VALUE(_cid_auto, _field##_auto, 0, true); /* Reset inactive flag */ \
				SET_CID_DEFAULT(_cid_manual, _field, false); \
				SET_CID_DEFAULT(_cid_auto, _field##_auto, false); \
			} \
		}

	CONTROL_AUTO_CID	(V4L2_CID_AUTOBRIGHTNESS,		V4L2_CID_BRIGHTNESS,				brightness);
	CONTROL_MANUAL_CID	(								V4L2_CID_CONTRAST,					contrast);
	CONTROL_MANUAL_CID	(								V4L2_CID_SATURATION,				saturation);
	CONTROL_AUTO_CID	(V4L2_CID_HUE_AUTO,				V4L2_CID_HUE,						hue);
	CONTROL_MANUAL_CID	(								V4L2_CID_GAMMA,						gamma);
	CONTROL_MANUAL_CID	(								V4L2_CID_SHARPNESS,					sharpness);
	CONTROL_MANUAL_CID	(								V4L2_CID_BACKLIGHT_COMPENSATION,	backlight_compensation);
	CONTROL_AUTO_CID	(V4L2_CID_AUTO_WHITE_BALANCE,	V4L2_CID_WHITE_BALANCE_TEMPERATURE,	white_balance);
	CONTROL_AUTO_CID	(V4L2_CID_AUTOGAIN,				V4L2_CID_GAIN,						gain);
	CONTROL_MANUAL_CID	(								V4L2_CID_COLORFX,					color_effect);
	CONTROL_MANUAL_CID	(								V4L2_CID_ROTATE,	   				rotate);
	CONTROL_MANUAL_CID	(								V4L2_CID_VFLIP,						flip_vertical);
	CONTROL_MANUAL_CID	(								V4L2_CID_HFLIP,						flip_horizontal);

#	undef CONTROL_AUTO_CID
#	undef CONTROL_MANUAL_CID
#	undef SET_CID_DEFAULT
#	undef SET_CID_VALUE
}

static int _device_query_control(
	device_s *dev, struct v4l2_queryctrl *query,
	const char *name, unsigned cid, bool quiet) {

	// cppcheck-suppress redundantPointerOp
	MEMSET_ZERO(*query);
	query->id = cid;

	if (D_XIOCTL(VIDIOC_QUERYCTRL, query) < 0 || query->flags & V4L2_CTRL_FLAG_DISABLED) {
		if (!quiet) {
			LOG_ERROR("Changing control %s is unsupported", name);
		}
		return -1;
	}
	return 0;
}

static void _device_set_control(
	device_s *dev, struct v4l2_queryctrl *query,
	const char *name, unsigned cid, int value, bool quiet) {

	if (value < query->minimum || value > query->maximum || value % query->step != 0) {
		if (!quiet) {
			LOG_ERROR("Invalid value %d of control %s: min=%d, max=%d, default=%d, step=%u",
				value, name, query->minimum, query->maximum, query->default_value, query->step);
		}
		return;
	}

	struct v4l2_control ctl = {0};
	ctl.id = cid;
	ctl.value = value;

	if (D_XIOCTL(VIDIOC_S_CTRL, &ctl) < 0) {
		if (!quiet) {
			LOG_PERROR("Can't set control %s", name);
		}
	} else if (!quiet) {
		LOG_INFO("Applying control %s: %d", name, ctl.value);
	}
}

static const char *_format_to_string_nullable(unsigned format) {
	for (unsigned index = 0; index < ARRAY_LEN(_FORMATS); ++index) {
		if (format == _FORMATS[index].format) {
			return _FORMATS[index].name;
		}
	}
	return NULL;
}

static const char *_format_to_string_supported(unsigned format) {
	const char *format_str = _format_to_string_nullable(format);
	return (format_str == NULL ? "unsupported" : format_str);
}

static const char *_standard_to_string(v4l2_std_id standard) {
	for (unsigned index = 0; index < ARRAY_LEN(_STANDARDS); ++index) {
		if (standard == _STANDARDS[index].standard) {
			return _STANDARDS[index].name;
		}
	}
	return _STANDARDS[0].name;
}

static const char *_io_method_to_string_supported(enum v4l2_memory io_method) {
	for (unsigned index = 0; index < ARRAY_LEN(_IO_METHODS); ++index) {
		if (io_method == _IO_METHODS[index].io_method) {
			return _IO_METHODS[index].name;
		}
	}
	return "unsupported";
}

#undef D_XIOCTL
#undef RUN
