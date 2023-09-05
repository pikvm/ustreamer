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
	const char *name; // cppcheck-suppress unusedStructMember
	const unsigned format; // cppcheck-suppress unusedStructMember
} _FORMATS[] = {
	{"YUYV",	V4L2_PIX_FMT_YUYV},
	{"UYVY",	V4L2_PIX_FMT_UYVY},
	{"RGB565",	V4L2_PIX_FMT_RGB565},
	{"RGB24",	V4L2_PIX_FMT_RGB24},
	{"MJPEG",	V4L2_PIX_FMT_MJPEG},
	{"JPEG",	V4L2_PIX_FMT_JPEG},
};

static const struct {
	const char *name; // cppcheck-suppress unusedStructMember
	const enum v4l2_memory io_method; // cppcheck-suppress unusedStructMember
} _IO_METHODS[] = {
	{"MMAP",	V4L2_MEMORY_MMAP},
	{"USERPTR",	V4L2_MEMORY_USERPTR},
};


static int _device_open_check_cap(us_device_s *dev);
static int _device_open_dv_timings(us_device_s *dev);
static int _device_apply_dv_timings(us_device_s *dev);
static int _device_open_format(us_device_s *dev, bool first);
static void _device_open_hw_fps(us_device_s *dev);
static void _device_open_jpeg_quality(us_device_s *dev);
static int _device_open_io_method(us_device_s *dev);
static int _device_open_io_method_mmap(us_device_s *dev);
static int _device_open_io_method_userptr(us_device_s *dev);
static int _device_open_queue_buffers(us_device_s *dev);
static int _device_apply_resolution(us_device_s *dev, unsigned width, unsigned height);

static void _device_apply_controls(us_device_s *dev);
static int _device_query_control(
	us_device_s *dev, struct v4l2_queryctrl *query,
	const char *name, unsigned cid, bool quiet);
static void _device_set_control(
	us_device_s *dev, const struct v4l2_queryctrl *query,
	const char *name, unsigned cid, int value, bool quiet);

static const char *_format_to_string_nullable(unsigned format);
static const char *_format_to_string_supported(unsigned format);
static const char *_standard_to_string(v4l2_std_id standard);
static const char *_io_method_to_string_supported(enum v4l2_memory io_method);


#define _RUN(x_next)	dev->run->x_next
#define _D_XIOCTL(...)	us_xioctl(_RUN(fd), __VA_ARGS__)


us_device_s *us_device_init(void) {
	us_device_runtime_s *run;
	US_CALLOC(run, 1);
	run->fd = -1;

	us_device_s *dev;
	US_CALLOC(dev, 1);
	dev->path = "/dev/video0";
	dev->width = 640;
	dev->height = 480;
	dev->format = V4L2_PIX_FMT_YUYV;
	dev->jpeg_quality = 80;
	dev->standard = V4L2_STD_UNKNOWN;
	dev->io_method = V4L2_MEMORY_MMAP;
	dev->n_bufs = us_get_cores_available() + 1;
	dev->min_frame_size = 128;
	dev->timeout = 1;
	dev->run = run;
	return dev;
}

void us_device_destroy(us_device_s *dev) {
	free(dev->run);
	free(dev);
}

int us_device_parse_format(const char *str) {
	US_ARRAY_ITERATE(_FORMATS, 0, item, {
		if (!strcasecmp(item->name, str)) {
			return item->format;
		}
	});
	return US_FORMAT_UNKNOWN;
}

v4l2_std_id us_device_parse_standard(const char *str) {
	US_ARRAY_ITERATE(_STANDARDS, 1, item, {
		if (!strcasecmp(item->name, str)) {
			return item->standard;
		}
	});
	return US_STANDARD_UNKNOWN;
}

int us_device_parse_io_method(const char *str) {
	US_ARRAY_ITERATE(_IO_METHODS, 0, item, {
		if (!strcasecmp(item->name, str)) {
			return item->io_method;
		}
	});
	return US_IO_METHOD_UNKNOWN;
}

int us_device_open(us_device_s *dev) {
	if ((_RUN(fd) = open(dev->path, O_RDWR|O_NONBLOCK)) < 0) {
		US_LOG_PERROR("Can't open device");
		goto error;
	}
	US_LOG_INFO("Device fd=%d opened", _RUN(fd));

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

	US_LOG_DEBUG("Device fd=%d initialized", _RUN(fd));
	return 0;

	error:
		us_device_close(dev);
		return -1;
}

void us_device_close(us_device_s *dev) {
	_RUN(persistent_timeout_reported) = false;

	if (_RUN(hw_bufs) != NULL) {
		US_LOG_DEBUG("Releasing device buffers ...");
		for (unsigned index = 0; index < _RUN(n_bufs); ++index) {
#			define HW(x_next) _RUN(hw_bufs)[index].x_next

			if (HW(dma_fd) >= 0) {
				close(HW(dma_fd));
				HW(dma_fd) = -1;
			}

			if (dev->io_method == V4L2_MEMORY_MMAP) {
				if (HW(raw.allocated) > 0 && HW(raw.data) != NULL) {
					if (munmap(HW(raw.data), HW(raw.allocated)) < 0) {
						US_LOG_PERROR("Can't unmap device buffer=%u", index);
					}
				}
			} else { // V4L2_MEMORY_USERPTR
				US_DELETE(HW(raw.data), free);
			}

#			undef HW
		}
		_RUN(n_bufs) = 0;
		free(_RUN(hw_bufs));
		_RUN(hw_bufs) = NULL;
	}

	if (_RUN(fd) >= 0) {
		US_LOG_DEBUG("Closing device ...");
		if (close(_RUN(fd)) < 0) {
			US_LOG_PERROR("Can't close device fd=%d", _RUN(fd));
		} else {
			US_LOG_INFO("Device fd=%d closed", _RUN(fd));
		}
		_RUN(fd) = -1;
	}
}

int us_device_export_to_dma(us_device_s *dev) {
#	define DMA_FD		_RUN(hw_bufs[index].dma_fd)

	for (unsigned index = 0; index < _RUN(n_bufs); ++index) {
		struct v4l2_exportbuffer exp = {0};
		exp.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		exp.index = index;

		US_LOG_DEBUG("Exporting device buffer=%u to DMA ...", index);
		if (_D_XIOCTL(VIDIOC_EXPBUF, &exp) < 0) {
			US_LOG_PERROR("Can't export device buffer=%u to DMA", index);
			goto error;
		}
		DMA_FD = exp.fd;
	}

	return 0;

	error:
		for (unsigned index = 0; index < _RUN(n_bufs); ++index) {
			if (DMA_FD >= 0) {
				close(DMA_FD);
				DMA_FD = -1;
			}
		}
		return -1;

#	undef DMA_FD
}

int us_device_switch_capturing(us_device_s *dev, bool enable) {
	if (enable != _RUN(capturing)) {
		enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

		US_LOG_DEBUG("%s device capturing ...", (enable ? "Starting" : "Stopping"));
		if (_D_XIOCTL((enable ? VIDIOC_STREAMON : VIDIOC_STREAMOFF), &type) < 0) {
			US_LOG_PERROR("Can't %s capturing", (enable ? "start" : "stop"));
			if (enable) {
				return -1;
			}
		}

		_RUN(capturing) = enable;
		US_LOG_INFO("Capturing %s", (enable ? "started" : "stopped"));
	}
	return 0;
}

int us_device_select(us_device_s *dev, bool *has_read, bool *has_write, bool *has_error) {
	int retval;

#	define INIT_FD_SET(x_set) \
		fd_set x_set; FD_ZERO(&x_set); FD_SET(_RUN(fd), &x_set);

	INIT_FD_SET(read_fds);
	INIT_FD_SET(write_fds);
	INIT_FD_SET(error_fds);

#	undef INIT_FD_SET

	struct timeval timeout;
	timeout.tv_sec = dev->timeout;
	timeout.tv_usec = 0;

	US_LOG_DEBUG("Calling select() on video device ...");

	retval = select(_RUN(fd) + 1, &read_fds, &write_fds, &error_fds, &timeout);
	if (retval > 0) {
		*has_read = FD_ISSET(_RUN(fd), &read_fds);
		*has_write = FD_ISSET(_RUN(fd), &write_fds);
		*has_error = FD_ISSET(_RUN(fd), &error_fds);
	} else {
		*has_read = false;
		*has_write = false;
		*has_error = false;
	}
	US_LOG_DEBUG("Device select() --> %d", retval);

	if (retval > 0) {
		_RUN(persistent_timeout_reported) = false;
	} else if (retval == 0) {
		if (dev->persistent) {
			if (!_RUN(persistent_timeout_reported)) {
				US_LOG_ERROR("Persistent device timeout (unplugged)");
				_RUN(persistent_timeout_reported) = true;
			}
		} else {
			// Если устройство не персистентное, то таймаут является ошибкой
			retval = -1;
		}
	}
	return retval;
}

static bool _validate_frame(us_device_s *dev, uint8_t *data, size_t len) {
	// Workaround for broken, corrupted frames:
	// Under low light conditions corrupted frames may get captured.
	// The good thing is such frames are quite small compared to the regular frames.
	// For example a VGA (640x480) webcam frame is normally >= 8kByte large,
	// corrupted frames are smaller.

	if (len < dev->min_frame_size) {
		US_LOG_DEBUG("Dropped too small frame, assuming it was broken. size=%lu", len);
		return false;
	}

	// Workaround for truncated JPEG frames:
	// Some inexpensive CCTV-style USB webcams such as the ELP-USB100W03M send
	// large amounts of these frames when using MJPEG streams. Check that the
	// buffer ends with JPEG end of image marker (0xFFD9)
	if (us_is_jpeg(dev->run->format)) {
		const uint8_t *const end_ptr = data + len;
		const uint8_t *const eoi_ptr = end_ptr - 2;

		if (eoi_ptr < data) {
			US_LOG_DEBUG("Discarding invalid frame, too small to be a valid JPEG, size=%lu", len);
			return false;
		}

		const uint16_t eoi_marker = (((uint16_t)(eoi_ptr[0]) << 8) | eoi_ptr[1]);
		if (eoi_marker != 0xFFD9) {
			US_LOG_DEBUG("Discarding truncated JPEG frame, eoi_marker=0x%04x size=%lu", eoi_marker, len);
			return false;
		}
	}

	return true;
}

#define _RELEASE_DEV_BUF(devbuf, why) \
	if (_D_XIOCTL(VIDIOC_QBUF, &devbuf) < 0) { \
		US_LOG_PERROR("Can't release device buffer=%u (" why ")", devbuf.index); \
		goto error_fatal; \
	}

int us_device_grab_buffer(us_device_s *dev, us_hw_buffer_s **hw) {
	// This attempts to grab the most recent *valid* frame from the device.
	// It does this by grabbing all currently available frame buffers from the
	// device, validating each one, and returning the most recent one that
	// contains a valid frame. If no valid frames are available, it returns
	// -2.

	US_LOG_DEBUG("Grabbing device buffer ...");

	us_device_stats_s *stats = &(dev->run->stats);

	struct v4l2_buffer captured_frame = {0};
	bool has_captured_frame = false;
	uint32_t skipped_frames = 0;

	while(true) {
		struct v4l2_buffer new_frame = {0};
		new_frame.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		new_frame.memory = dev->io_method;

		const bool has_new_frame = (_D_XIOCTL(VIDIOC_DQBUF, &new_frame) >= 0);

		if(has_new_frame) {
			stats->captured_frames++;

			// Check to make sure that the buffer we grabbed is under our
			// control.
			if (new_frame.index >= dev->run->n_bufs) {
				US_LOG_ERROR("V4L2 error: grabbed invalid device buffer=%u, n_bufs=%u", new_frame.index, _RUN(n_bufs));
				goto error_fatal;
			}

			// Validate the frame.
			if(_validate_frame(dev, dev->run->hw_bufs[new_frame.index].raw.data, new_frame.bytesused)) {
				// if we grabbed a frame in a previous iteration of this loop,
				// release its buffer since we no longer need it.
				if (has_captured_frame) {
					_RELEASE_DEV_BUF(captured_frame, "skipped frame");
					skipped_frames++;
					stats->skipped_frames++;
				}

				memcpy(&captured_frame, &new_frame, sizeof(struct v4l2_buffer));
				has_captured_frame = true;
			}

			// Skip invalid frames, releasing their buffer so they can be used
			// to grab a new frame.
			else {
				_RELEASE_DEV_BUF(new_frame, "invalid frame");
				stats->invalid_frames++;

				if(stats->invalid_frames % 100 == 0) {
					US_LOG_INFO(
						"Discarded %lu invalid frames out of %lu total captured (%.0f%%).",
						stats->invalid_frames, stats->captured_frames,
						((float)(stats->invalid_frames) / (float)stats->captured_frames) * 100.f);
				}
			}
		}
		// EAGAIN means the device has no new frames to give.
		else if (errno == EAGAIN) {
			// If we managed to grab a valid frame, we're good to go.
			if (has_captured_frame) {
				break;
			// Otherwise, tell the caller we didn't get a valid frame from
			// the device yet.
			} else {
				goto error_no_valid_frame;
			}
		}
		else {
			US_LOG_PERROR("Can't grab device buffer");
			goto error_fatal;
		}
	}

	stats->valid_frames++;

	us_hw_buffer_s* dst = &(dev->run->hw_bufs[captured_frame.index]);

	dst->grabbed = true;
	dst->raw.dma_fd = dst->dma_fd;
	dst->raw.used = captured_frame.bytesused;
	dst->raw.width = dev->run->width;
	dst->raw.height = dev->run->height;
	dst->raw.format = dev->run->format;
	dst->raw.stride = dev->run->stride;
	dst->raw.online = true;
	memcpy(&(dst->buf), &captured_frame, sizeof(struct v4l2_buffer));
	dst->raw.grab_ts = (long double)((captured_frame.timestamp.tv_sec * (uint64_t)1000) + (captured_frame.timestamp.tv_usec / 1000)) / 1000;

	US_LOG_DEBUG("Grabbed new frame: buffer=%u, size=%u, grab_ts=%.3Lf, latency=%.3Lf, skipped=%u",
		captured_frame.index, captured_frame.bytesused, dst->raw.grab_ts, us_get_now_monotonic() - dst->raw.grab_ts, skipped_frames);

	*hw = dst;
	return captured_frame.index;

error_fatal:
	return -1;

error_no_valid_frame:
	return -2;
}

int us_device_release_buffer(us_device_s *dev, us_hw_buffer_s *hw) {
	const unsigned index = hw->buf.index;
	US_LOG_DEBUG("Releasing device buffer=%u ...", index);

	if (_D_XIOCTL(VIDIOC_QBUF, &hw->buf) < 0) {
		US_LOG_PERROR("Can't release device buffer=%u", index);
		return -1;
	}

	hw->grabbed = false;
	return 0;
}

int us_device_consume_event(us_device_s *dev) {
	struct v4l2_event event;

	US_LOG_DEBUG("Consuming V4L2 event ...");
	if (_D_XIOCTL(VIDIOC_DQEVENT, &event) == 0) {
		switch (event.type) {
			case V4L2_EVENT_SOURCE_CHANGE:
				US_LOG_INFO("Got V4L2_EVENT_SOURCE_CHANGE: source changed");
				return -1;
			case V4L2_EVENT_EOS:
				US_LOG_INFO("Got V4L2_EVENT_EOS: end of stream (ignored)");
				return 0;
		}
	} else {
		US_LOG_PERROR("Got some V4L2 device event, but where is it? ");
	}
	return 0;
}

static int _device_open_check_cap(us_device_s *dev) {
	struct v4l2_capability cap = {0};

	US_LOG_DEBUG("Querying device capabilities ...");
	if (_D_XIOCTL(VIDIOC_QUERYCAP, &cap) < 0) {
		US_LOG_PERROR("Can't query device capabilities");
		return -1;
	}

	if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
		US_LOG_ERROR("Video capture is not supported by device");
		return -1;
	}

	if (!(cap.capabilities & V4L2_CAP_STREAMING)) {
		US_LOG_ERROR("Device doesn't support streaming IO");
		return -1;
	}

	int input = dev->input; // Needs a pointer to int for ioctl()
	US_LOG_INFO("Using input channel: %d", input);
	if (_D_XIOCTL(VIDIOC_S_INPUT, &input) < 0) {
		US_LOG_ERROR("Can't set input channel");
		return -1;
	}

	if (dev->standard != V4L2_STD_UNKNOWN) {
		US_LOG_INFO("Using TV standard: %s", _standard_to_string(dev->standard));
		if (_D_XIOCTL(VIDIOC_S_STD, &dev->standard) < 0) {
			US_LOG_ERROR("Can't set video standard");
			return -1;
		}
	} else {
		US_LOG_DEBUG("Using TV standard: DEFAULT");
	}
	return 0;
}

static int _device_open_dv_timings(us_device_s *dev) {
	_device_apply_resolution(dev, dev->width, dev->height);
	if (dev->dv_timings) {
		US_LOG_DEBUG("Using DV-timings");

		if (_device_apply_dv_timings(dev) < 0) {
			return -1;
		}

		struct v4l2_event_subscription sub = {0};
		sub.type = V4L2_EVENT_SOURCE_CHANGE;

		US_LOG_DEBUG("Subscribing to DV-timings events ...")
		if (_D_XIOCTL(VIDIOC_SUBSCRIBE_EVENT, &sub) < 0) {
			US_LOG_PERROR("Can't subscribe to DV-timings events");
			return -1;
		}
	}
	return 0;
}

static int _device_apply_dv_timings(us_device_s *dev) {
	struct v4l2_dv_timings dv = {0};

	US_LOG_DEBUG("Calling us_xioctl(VIDIOC_QUERY_DV_TIMINGS) ...");
	if (_D_XIOCTL(VIDIOC_QUERY_DV_TIMINGS, &dv) == 0) {
		if (dv.type == V4L2_DV_BT_656_1120) {
			// See v4l2_print_dv_timings() in the kernel
			const unsigned htot = V4L2_DV_BT_FRAME_WIDTH(&dv.bt);
			const unsigned vtot = V4L2_DV_BT_FRAME_HEIGHT(&dv.bt) / (dv.bt.interlaced ? 2 : 1);
			const unsigned fps = ((htot * vtot) > 0 ? ((100 * (uint64_t)dv.bt.pixelclock)) / (htot * vtot) : 0);
			US_LOG_INFO("Got new DV-timings: %ux%u%s%u.%02u, pixclk=%llu, vsync=%u, hsync=%u",
				dv.bt.width, dv.bt.height, (dv.bt.interlaced ? "i" : "p"), fps / 100, fps % 100,
				(unsigned long long)dv.bt.pixelclock, dv.bt.vsync, dv.bt.hsync); // See #11 about %llu
		} else {
			US_LOG_INFO("Got new DV-timings: %ux%u, pixclk=%llu, vsync=%u, hsync=%u",
				dv.bt.width, dv.bt.height,
				(unsigned long long)dv.bt.pixelclock, dv.bt.vsync, dv.bt.hsync);
		}

		US_LOG_DEBUG("Calling us_xioctl(VIDIOC_S_DV_TIMINGS) ...");
		if (_D_XIOCTL(VIDIOC_S_DV_TIMINGS, &dv) < 0) {
			US_LOG_PERROR("Failed to set DV-timings");
			return -1;
		}

		if (_device_apply_resolution(dev, dv.bt.width, dv.bt.height) < 0) {
			return -1;
		}

	} else {
		US_LOG_DEBUG("Calling us_xioctl(VIDIOC_QUERYSTD) ...");
		if (_D_XIOCTL(VIDIOC_QUERYSTD, &dev->standard) == 0) {
			US_LOG_INFO("Applying the new VIDIOC_S_STD: %s ...", _standard_to_string(dev->standard));
			if (_D_XIOCTL(VIDIOC_S_STD, &dev->standard) < 0) {
				US_LOG_PERROR("Can't set video standard");
				return -1;
			}
		}
	}
	return 0;
}

static int _device_open_format(us_device_s *dev, bool first) {
	const unsigned stride = us_align_size(_RUN(width), 32) << 1;

	struct v4l2_format fmt = {0};
	fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	fmt.fmt.pix.width = _RUN(width);
	fmt.fmt.pix.height = _RUN(height);
	fmt.fmt.pix.pixelformat = dev->format;
	fmt.fmt.pix.field = V4L2_FIELD_ANY;
	fmt.fmt.pix.bytesperline = stride;

	// Set format
	US_LOG_DEBUG("Probing device format=%s, stride=%u, resolution=%ux%u ...",
		_format_to_string_supported(dev->format), stride, _RUN(width), _RUN(height));
	if (_D_XIOCTL(VIDIOC_S_FMT, &fmt) < 0) {
		US_LOG_PERROR("Can't set device format");
		return -1;
	}

	// Check resolution
	bool retry = false;
	if (fmt.fmt.pix.width != _RUN(width) || fmt.fmt.pix.height != _RUN(height)) {
		US_LOG_ERROR("Requested resolution=%ux%u is unavailable", _RUN(width), _RUN(height));
		retry = true;
	}
	if (_device_apply_resolution(dev, fmt.fmt.pix.width, fmt.fmt.pix.height) < 0) {
		return -1;
	}
	if (first && retry) {
		return _device_open_format(dev, false);
	}
	US_LOG_INFO("Using resolution: %ux%u", _RUN(width), _RUN(height));

	// Check format
	if (fmt.fmt.pix.pixelformat != dev->format) {
		US_LOG_ERROR("Could not obtain the requested format=%s; driver gave us %s",
			_format_to_string_supported(dev->format),
			_format_to_string_supported(fmt.fmt.pix.pixelformat));

		char *format_str;
		if ((format_str = (char *)_format_to_string_nullable(fmt.fmt.pix.pixelformat)) != NULL) {
			US_LOG_INFO("Falling back to format=%s", format_str);
		} else {
			char fourcc_str[8];
			US_LOG_ERROR("Unsupported format=%s (fourcc)",
				us_fourcc_to_string(fmt.fmt.pix.pixelformat, fourcc_str, 8));
			return -1;
		}
	}

	_RUN(format) = fmt.fmt.pix.pixelformat;
	US_LOG_INFO("Using format: %s", _format_to_string_supported(_RUN(format)));

	_RUN(stride) = fmt.fmt.pix.bytesperline;
	_RUN(raw_size) = fmt.fmt.pix.sizeimage; // Only for userptr
	return 0;
}

static void _device_open_hw_fps(us_device_s *dev) {
	_RUN(hw_fps) = 0;

	struct v4l2_streamparm setfps = {0};
	setfps.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

	US_LOG_DEBUG("Querying HW FPS ...");
	if (_D_XIOCTL(VIDIOC_G_PARM, &setfps) < 0) {
		if (errno == ENOTTY) { // Quiet message for TC358743
			US_LOG_INFO("Querying HW FPS changing is not supported");
		} else {
			US_LOG_PERROR("Can't query HW FPS changing");
		}
		return;
	}

	if (!(setfps.parm.capture.capability & V4L2_CAP_TIMEPERFRAME)) {
		US_LOG_INFO("Changing HW FPS is not supported");
		return;
	}

#	define SETFPS_TPF(x_next) setfps.parm.capture.timeperframe.x_next

	US_MEMSET_ZERO(setfps);
	setfps.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	SETFPS_TPF(numerator) = 1;
	SETFPS_TPF(denominator) = (dev->desired_fps == 0 ? 255 : dev->desired_fps);

	if (_D_XIOCTL(VIDIOC_S_PARM, &setfps) < 0) {
		US_LOG_PERROR("Can't set HW FPS");
		return;
	}

	if (SETFPS_TPF(numerator) != 1) {
		US_LOG_ERROR("Invalid HW FPS numerator: %u != 1", SETFPS_TPF(numerator));
		return;
	}

	if (SETFPS_TPF(denominator) == 0) { // Не знаю, бывает ли так, но пускай на всякий случай
		US_LOG_ERROR("Invalid HW FPS denominator: 0");
		return;
	}

	_RUN(hw_fps) = SETFPS_TPF(denominator);
	if (dev->desired_fps != _RUN(hw_fps)) {
		US_LOG_INFO("Using HW FPS: %u -> %u (coerced)", dev->desired_fps, _RUN(hw_fps));
	} else {
		US_LOG_INFO("Using HW FPS: %u", _RUN(hw_fps));
	}

#	undef SETFPS_TPF
}

static void _device_open_jpeg_quality(us_device_s *dev) {
	unsigned quality = 0;

	if (us_is_jpeg(_RUN(format))) {
		struct v4l2_jpegcompression comp = {0};

		if (_D_XIOCTL(VIDIOC_G_JPEGCOMP, &comp) < 0) {
			US_LOG_ERROR("Device doesn't support setting of HW encoding quality parameters");
		} else {
			comp.quality = dev->jpeg_quality;
			if (_D_XIOCTL(VIDIOC_S_JPEGCOMP, &comp) < 0) {
				US_LOG_ERROR("Can't change MJPEG quality for JPEG source with HW pass-through encoder");
			} else {
				quality = dev->jpeg_quality;
			}
		}
	}

	_RUN(jpeg_quality) = quality;
}

static int _device_open_io_method(us_device_s *dev) {
	US_LOG_INFO("Using IO method: %s", _io_method_to_string_supported(dev->io_method));
	switch (dev->io_method) {
		case V4L2_MEMORY_MMAP: return _device_open_io_method_mmap(dev);
		case V4L2_MEMORY_USERPTR: return _device_open_io_method_userptr(dev);
		default: assert(0 && "Unsupported IO method");
	}
	return -1;
}

static int _device_open_io_method_mmap(us_device_s *dev) {
	struct v4l2_requestbuffers req = {0};
	req.count = dev->n_bufs;
	req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	req.memory = V4L2_MEMORY_MMAP;

	US_LOG_DEBUG("Requesting %u device buffers for MMAP ...", req.count);
	if (_D_XIOCTL(VIDIOC_REQBUFS, &req) < 0) {
		US_LOG_PERROR("Device '%s' doesn't support MMAP method", dev->path);
		return -1;
	}

	if (req.count < 1) {
		US_LOG_ERROR("Insufficient buffer memory: %u", req.count);
		return -1;
	} else {
		US_LOG_INFO("Requested %u device buffers, got %u", dev->n_bufs, req.count);
	}

	US_LOG_DEBUG("Allocating device buffers ...");

	US_CALLOC(_RUN(hw_bufs), req.count);
	for (_RUN(n_bufs) = 0; _RUN(n_bufs) < req.count; ++_RUN(n_bufs)) {
		struct v4l2_buffer buf = {0};
		buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buf.memory = V4L2_MEMORY_MMAP;
		buf.index = _RUN(n_bufs);

		US_LOG_DEBUG("Calling us_xioctl(VIDIOC_QUERYBUF) for device buffer=%u ...", _RUN(n_bufs));
		if (_D_XIOCTL(VIDIOC_QUERYBUF, &buf) < 0) {
			US_LOG_PERROR("Can't VIDIOC_QUERYBUF");
			return -1;
		}

#		define HW(x_next) _RUN(hw_bufs)[_RUN(n_bufs)].x_next

		HW(dma_fd) = -1;

		US_LOG_DEBUG("Mapping device buffer=%u ...", _RUN(n_bufs));
		if ((HW(raw.data) = mmap(
			NULL,
			buf.length,
			PROT_READ | PROT_WRITE,
			MAP_SHARED,
			_RUN(fd),
			buf.m.offset
		)) == MAP_FAILED) {
			US_LOG_PERROR("Can't map device buffer=%u", _RUN(n_bufs));
			return -1;
		}
		assert(HW(raw.data) != NULL);
		HW(raw.allocated) = buf.length;

#		undef HW
	}
	return 0;
}

static int _device_open_io_method_userptr(us_device_s *dev) {
	struct v4l2_requestbuffers req = {0};
	req.count = dev->n_bufs;
	req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	req.memory = V4L2_MEMORY_USERPTR;

	US_LOG_DEBUG("Requesting %u device buffers for USERPTR ...", req.count);
	if (_D_XIOCTL(VIDIOC_REQBUFS, &req) < 0) {
		US_LOG_PERROR("Device '%s' doesn't support USERPTR method", dev->path);
		return -1;
	}

	if (req.count < 1) {
		US_LOG_ERROR("Insufficient buffer memory: %u", req.count);
		return -1;
	} else {
		US_LOG_INFO("Requested %u device buffers, got %u", dev->n_bufs, req.count);
	}

	US_LOG_DEBUG("Allocating device buffers ...");

	US_CALLOC(_RUN(hw_bufs), req.count);

	const unsigned page_size = getpagesize();
	const unsigned buf_size = us_align_size(_RUN(raw_size), page_size);

	for (_RUN(n_bufs) = 0; _RUN(n_bufs) < req.count; ++_RUN(n_bufs)) {
#       define HW(x_next) _RUN(hw_bufs)[_RUN(n_bufs)].x_next
		assert((HW(raw.data) = aligned_alloc(page_size, buf_size)) != NULL);
		memset(HW(raw.data), 0, buf_size);
		HW(raw.allocated) = buf_size;
#		undef HW
	}
	return 0;
}

static int _device_open_queue_buffers(us_device_s *dev) {
	for (unsigned index = 0; index < _RUN(n_bufs); ++index) {
		struct v4l2_buffer buf = {0};
		buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buf.memory = dev->io_method;
		buf.index = index;
		if (dev->io_method == V4L2_MEMORY_USERPTR) {
			buf.m.userptr = (unsigned long)_RUN(hw_bufs)[index].raw.data;
			buf.length = _RUN(hw_bufs)[index].raw.allocated;
		}

		US_LOG_DEBUG("Calling us_xioctl(VIDIOC_QBUF) for buffer=%u ...", index);
		if (_D_XIOCTL(VIDIOC_QBUF, &buf) < 0) {
			US_LOG_PERROR("Can't VIDIOC_QBUF");
			return -1;
		}
	}
	return 0;
}

static int _device_apply_resolution(us_device_s *dev, unsigned width, unsigned height) {
	// Тут VIDEO_MIN_* не используются из-за странностей минимального разрешения при отсутствии сигнала
	// у некоторых устройств, например TC358743
	if (
		width == 0 || width > US_VIDEO_MAX_WIDTH
		|| height == 0 || height > US_VIDEO_MAX_HEIGHT
	) {
		US_LOG_ERROR("Requested forbidden resolution=%ux%u: min=1x1, max=%ux%u",
			width, height, US_VIDEO_MAX_WIDTH, US_VIDEO_MAX_HEIGHT);
		return -1;
	}
	_RUN(width) = width;
	_RUN(height) = height;
	return 0;
}

static void _device_apply_controls(us_device_s *dev) {
#	define SET_CID_VALUE(x_cid, x_field, x_value, x_quiet) { \
			struct v4l2_queryctrl m_query; \
			if (_device_query_control(dev, &m_query, #x_field, x_cid, x_quiet) == 0) { \
				_device_set_control(dev, &m_query, #x_field, x_cid, x_value, x_quiet); \
			} \
		}

#	define SET_CID_DEFAULT(x_cid, x_field, x_quiet) { \
			struct v4l2_queryctrl m_query; \
			if (_device_query_control(dev, &m_query, #x_field, x_cid, x_quiet) == 0) { \
				_device_set_control(dev, &m_query, #x_field, x_cid, m_query.default_value, x_quiet); \
			} \
		}

#	define CONTROL_MANUAL_CID(x_cid, x_field) { \
			if (dev->ctl.x_field.mode == CTL_MODE_VALUE) { \
				SET_CID_VALUE(x_cid, x_field, dev->ctl.x_field.value, false); \
			} else if (dev->ctl.x_field.mode == CTL_MODE_DEFAULT) { \
				SET_CID_DEFAULT(x_cid, x_field, false); \
			} \
		}

#	define CONTROL_AUTO_CID(x_cid_auto, x_cid_manual, x_field) { \
			if (dev->ctl.x_field.mode == CTL_MODE_VALUE) { \
				SET_CID_VALUE(x_cid_auto, x_field##_auto, 0, true); \
				SET_CID_VALUE(x_cid_manual, x_field, dev->ctl.x_field.value, false); \
			} else if (dev->ctl.x_field.mode == CTL_MODE_AUTO) { \
				SET_CID_VALUE(x_cid_auto, x_field##_auto, 1, false); \
			} else if (dev->ctl.x_field.mode == CTL_MODE_DEFAULT) { \
				SET_CID_VALUE(x_cid_auto, x_field##_auto, 0, true); /* Reset inactive flag */ \
				SET_CID_DEFAULT(x_cid_manual, x_field, false); \
				SET_CID_DEFAULT(x_cid_auto, x_field##_auto, false); \
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
	us_device_s *dev, struct v4l2_queryctrl *query,
	const char *name, unsigned cid, bool quiet) {

	// cppcheck-suppress redundantPointerOp
	US_MEMSET_ZERO(*query);
	query->id = cid;

	if (_D_XIOCTL(VIDIOC_QUERYCTRL, query) < 0 || query->flags & V4L2_CTRL_FLAG_DISABLED) {
		if (!quiet) {
			US_LOG_ERROR("Changing control %s is unsupported", name);
		}
		return -1;
	}
	return 0;
}

static void _device_set_control(
	us_device_s *dev, const struct v4l2_queryctrl *query,
	const char *name, unsigned cid, int value, bool quiet) {

	if (value < query->minimum || value > query->maximum || value % query->step != 0) {
		if (!quiet) {
			US_LOG_ERROR("Invalid value %d of control %s: min=%d, max=%d, default=%d, step=%u",
				value, name, query->minimum, query->maximum, query->default_value, query->step);
		}
		return;
	}

	struct v4l2_control ctl = {0};
	ctl.id = cid;
	ctl.value = value;

	if (_D_XIOCTL(VIDIOC_S_CTRL, &ctl) < 0) {
		if (!quiet) {
			US_LOG_PERROR("Can't set control %s", name);
		}
	} else if (!quiet) {
		US_LOG_INFO("Applying control %s: %d", name, ctl.value);
	}
}

static const char *_format_to_string_nullable(unsigned format) {
	US_ARRAY_ITERATE(_FORMATS, 0, item, {
		if (item->format == format) {
			return item->name;
		}
	});
	return NULL;
}

static const char *_format_to_string_supported(unsigned format) {
	const char *const format_str = _format_to_string_nullable(format);
	return (format_str == NULL ? "unsupported" : format_str);
}

static const char *_standard_to_string(v4l2_std_id standard) {
	US_ARRAY_ITERATE(_STANDARDS, 0, item, {
		if (item->standard == standard) {
			return item->name;
		}
	});
	return _STANDARDS[0].name;
}

static const char *_io_method_to_string_supported(enum v4l2_memory io_method) {
	US_ARRAY_ITERATE(_IO_METHODS, 0, item, {
		if (item->io_method == io_method) {
			return item->name;
		}
	});
	return "unsupported";
}
