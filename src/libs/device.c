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

#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <assert.h>

#include <sys/select.h>
#include <sys/mman.h>
#include <sys/time.h>

#include <pthread.h>
#include <linux/videodev2.h>
#include <linux/v4l2-controls.h>

#include "types.h"
#include "tools.h"
#include "array.h"
#include "logging.h"
#include "threading.h"
#include "frame.h"
#include "xioctl.h"


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
	const uint format; // cppcheck-suppress unusedStructMember
} _FORMATS[] = {
	{"YUYV",	V4L2_PIX_FMT_YUYV},
	{"YVYU",	V4L2_PIX_FMT_YVYU},
	{"UYVY",	V4L2_PIX_FMT_UYVY},
	{"RGB565",	V4L2_PIX_FMT_RGB565},
	{"RGB24",	V4L2_PIX_FMT_RGB24},
	{"BGR24",	V4L2_PIX_FMT_BGR24},
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

static int _device_consume_event(us_device_s *dev);
static void _v4l2_buffer_copy(const struct v4l2_buffer *src, struct v4l2_buffer *dest);
static bool _device_is_buffer_valid(us_device_s *dev, const struct v4l2_buffer *buf, const u8 *data);
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
static int _device_open_export_to_dma(us_device_s *dev);
static int _device_apply_resolution(us_device_s *dev, uint width, uint height, float hz);

static void _device_apply_controls(us_device_s *dev);
static int _device_query_control(
	us_device_s *dev, struct v4l2_queryctrl *query,
	const char *name, uint cid, bool quiet);
static void _device_set_control(
	us_device_s *dev, const struct v4l2_queryctrl *query,
	const char *name, uint cid, int value, bool quiet);

static const char *_format_to_string_nullable(uint format);
static const char *_format_to_string_supported(uint format);
static const char *_standard_to_string(v4l2_std_id standard);
static const char *_io_method_to_string_supported(enum v4l2_memory io_method);


#define _D_LOG_ERROR(x_msg, ...)	US_LOG_ERROR("CAP: " x_msg, ##__VA_ARGS__)
#define _D_LOG_PERROR(x_msg, ...)	US_LOG_PERROR("CAP: " x_msg, ##__VA_ARGS__)
#define _D_LOG_INFO(x_msg, ...)		US_LOG_INFO("CAP: " x_msg, ##__VA_ARGS__)
#define _D_LOG_VERBOSE(x_msg, ...)	US_LOG_VERBOSE("CAP: " x_msg, ##__VA_ARGS__)
#define _D_LOG_DEBUG(x_msg, ...)	US_LOG_DEBUG("CAP: " x_msg, ##__VA_ARGS__)


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
	us_device_runtime_s *const run = dev->run;

	if ((run->fd = open(dev->path, O_RDWR|O_NONBLOCK)) < 0) {
		_D_LOG_PERROR("Can't open device");
		goto error;
	}
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
	if (dev->dma_export && !us_is_jpeg(run->format)) {
		// uStreamer doesn't have any component that could handle JPEG capture via DMA
		run->dma = !_device_open_export_to_dma(dev);
		if (!run->dma && dev->dma_required) {
			goto error;
		}
	}
	_device_apply_controls(dev);

	enum v4l2_buf_type type = run->capture_type;
	if (us_xioctl(run->fd, VIDIOC_STREAMON, &type) < 0) {
		_D_LOG_PERROR("Can't start capturing");
		goto error;
	}
	run->streamon = true;
	_D_LOG_INFO("Capturing started");
	return 0;

error:
	us_device_close(dev);
	return -1;
}

void us_device_close(us_device_s *dev) {
	us_device_runtime_s *const run = dev->run;

	if (run->streamon) {
		enum v4l2_buf_type type = run->capture_type;
		if (us_xioctl(run->fd, VIDIOC_STREAMOFF, &type) < 0) {
			_D_LOG_PERROR("Can't stop capturing");
		}
		run->streamon = false;
		_D_LOG_INFO("Capturing stopped");
	}

	if (run->hw_bufs != NULL) {
		_D_LOG_DEBUG("Releasing device buffers ...");
		for (uint index = 0; index < run->n_bufs; ++index) {
			us_hw_buffer_s *hw = &run->hw_bufs[index];

			US_CLOSE_FD(hw->dma_fd);

			if (dev->io_method == V4L2_MEMORY_MMAP) {
				if (hw->raw.allocated > 0 && hw->raw.data != NULL) {
					if (munmap(hw->raw.data, hw->raw.allocated) < 0) {
						_D_LOG_PERROR("Can't unmap device buffer=%u", index);
					}
				}
			} else { // V4L2_MEMORY_USERPTR
				US_DELETE(hw->raw.data, free);
			}

			if (run->capture_mplane) {
				free(hw->buf.m.planes);
			}
		}
		US_DELETE(run->hw_bufs, free);
		run->n_bufs = 0;
	}

	US_CLOSE_FD(run->fd);
	run->persistent_timeout_reported = false;
}

int us_device_wait_buffer(us_device_s *dev) {
	us_device_runtime_s *const run = dev->run;

#	define INIT_FD_SET(x_set) \
		fd_set x_set; FD_ZERO(&x_set); FD_SET(run->fd, &x_set);
	INIT_FD_SET(read_fds);
	INIT_FD_SET(error_fds);
#	undef INIT_FD_SET

	// Раньше мы проверяли и has_write, но потом выяснилось, что libcamerify зачем-то
	// генерирует эвенты на запись, вероятно ошибочно. Судя по всему, игнорирование
	// has_write не делает никому плохо.

	struct timeval timeout;
	timeout.tv_sec = dev->timeout;
	timeout.tv_usec = 0;

	_D_LOG_DEBUG("Calling select() on video device ...");

	bool has_read = false;
	bool has_error = false;
	const int selected = select(run->fd + 1, &read_fds, NULL, &error_fds, &timeout);
	if (selected > 0) {
		has_read = FD_ISSET(run->fd, &read_fds);
		has_error = FD_ISSET(run->fd, &error_fds);
	}
	_D_LOG_DEBUG("Device select() --> %d; has_read=%d, has_error=%d", selected, has_read, has_error);

	if (selected < 0) {
		if (errno != EINTR) {
			_D_LOG_PERROR("Device select() error");
		}
		return -1;
	} else if (selected == 0) {
		if (dev->persistent) {
			if (!run->persistent_timeout_reported) {
				_D_LOG_ERROR("Persistent device timeout (unplugged)");
				run->persistent_timeout_reported = true;
			}
		} else {
			// Если устройство не персистентное, то таймаут является ошибкой
			return -1;
		}
		return -2; // No new frames
	} else {
		run->persistent_timeout_reported = false;
		if (has_error && _device_consume_event(dev) < 0) {
			return -1; // Restart required
		}
	}
	return 0;
}

static int _device_consume_event(us_device_s *dev) {
	struct v4l2_event event;
	if (us_xioctl(dev->run->fd, VIDIOC_DQEVENT, &event) < 0) {
		_D_LOG_PERROR("Can't consume V4L2 event");
		return -1;
	}
	switch (event.type) {
		case V4L2_EVENT_SOURCE_CHANGE:
			_D_LOG_INFO("Got V4L2_EVENT_SOURCE_CHANGE: Source changed");
			return -1;
		case V4L2_EVENT_EOS:
			_D_LOG_INFO("Got V4L2_EVENT_EOS: End of stream");
			return -1;
	}
	return 0;
}

int us_device_grab_buffer(us_device_s *dev, us_hw_buffer_s **hw) {
	us_device_runtime_s *const run = dev->run;

	*hw = NULL;

	struct v4l2_buffer buf = {0};
	struct v4l2_plane buf_planes[VIDEO_MAX_PLANES] = {0};
	if (run->capture_mplane) {
		// Just for _v4l2_buffer_copy(), buf.length is not needed here
		buf.m.planes = buf_planes;
	}

	bool buf_got = false;
	uint skipped = 0;
	bool broken = false;

	_D_LOG_DEBUG("Grabbing device buffer ...");

	do {
		struct v4l2_buffer new = {0};
		struct v4l2_plane new_planes[VIDEO_MAX_PLANES] = {0};
		new.type = run->capture_type;
		new.memory = dev->io_method;
		if (run->capture_mplane) {
			new.length = VIDEO_MAX_PLANES;
			new.m.planes = new_planes;
		}
		
		const bool new_got = (us_xioctl(run->fd, VIDIOC_DQBUF, &new) >= 0);

		if (new_got) {
			if (new.index >= run->n_bufs) {
				_D_LOG_ERROR("V4L2 error: grabbed invalid device buffer=%u, n_bufs=%u", new.index, run->n_bufs);
				return -1;
			}

#			define GRABBED(x_buf) run->hw_bufs[x_buf.index].grabbed
#			define FRAME_DATA(x_buf) run->hw_bufs[x_buf.index].raw.data

			if (GRABBED(new)) {
				_D_LOG_ERROR("V4L2 error: grabbed device buffer=%u is already used", new.index);
				return -1;
			}
			GRABBED(new) = true;

			if (run->capture_mplane) {
				new.bytesused = new.m.planes[0].bytesused;
			}

			broken = !_device_is_buffer_valid(dev, &new, FRAME_DATA(new));
			if (broken) {
				_D_LOG_DEBUG("Releasing device buffer=%u (broken frame) ...", new.index);
				if (us_xioctl(run->fd, VIDIOC_QBUF, &new) < 0) {
					_D_LOG_PERROR("Can't release device buffer=%u (broken frame)", new.index);
					return -1;
				}
				GRABBED(new) = false;
				continue;
			}

			if (buf_got) {
				if (us_xioctl(run->fd, VIDIOC_QBUF, &buf) < 0) {
					_D_LOG_PERROR("Can't release device buffer=%u (skipped frame)", buf.index);
					return -1;
				}
				GRABBED(buf) = false;
				++skipped;
				// buf_got = false;
			}

#			undef GRABBED
#			undef FRAME_DATA

			_v4l2_buffer_copy(&new, &buf);
			buf_got = true;

		} else {
			if (errno == EAGAIN) {
				if (buf_got) {
					break; // Process any latest valid frame
				} else if (broken) {
					return -2; // If we have only broken frames on this capture session
				}
			}
			_D_LOG_PERROR("Can't grab device buffer");
			return -1;
		}
	} while (true);

	*hw = &run->hw_bufs[buf.index];
	(*hw)->raw.dma_fd = (*hw)->dma_fd;
	(*hw)->raw.used = buf.bytesused;
	(*hw)->raw.width = run->width;
	(*hw)->raw.height = run->height;
	(*hw)->raw.format = run->format;
	(*hw)->raw.stride = run->stride;
	(*hw)->raw.online = true;
	_v4l2_buffer_copy(&buf, &(*hw)->buf);
	(*hw)->raw.grab_ts = (ldf)((buf.timestamp.tv_sec * (u64)1000) + (buf.timestamp.tv_usec / 1000)) / 1000;

	_D_LOG_DEBUG("Grabbed new frame: buffer=%u, bytesused=%u, grab_ts=%.3Lf, latency=%.3Lf, skipped=%u",
		buf.index, buf.bytesused, (*hw)->raw.grab_ts, us_get_now_monotonic() - (*hw)->raw.grab_ts, skipped);
	return buf.index;
}

int us_device_release_buffer(us_device_s *dev, us_hw_buffer_s *hw) {
	const uint index = hw->buf.index;
	_D_LOG_DEBUG("Releasing device buffer=%u ...", index);
	if (us_xioctl(dev->run->fd, VIDIOC_QBUF, &hw->buf) < 0) {
		_D_LOG_PERROR("Can't release device buffer=%u", index);
		return -1;
	}
	hw->grabbed = false;
	return 0;
}

static void _v4l2_buffer_copy(const struct v4l2_buffer *src, struct v4l2_buffer *dest) {
	struct v4l2_plane *dest_planes = dest->m.planes;
	memcpy(dest, src, sizeof(struct v4l2_buffer));
	if (src->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
		assert(dest_planes);
		dest->m.planes = dest_planes;
		memcpy(dest->m.planes, src->m.planes, sizeof(struct v4l2_plane) * VIDEO_MAX_PLANES);
	}
}

bool _device_is_buffer_valid(us_device_s *dev, const struct v4l2_buffer *buf, const u8 *data) {
	// Workaround for broken, corrupted frames:
	// Under low light conditions corrupted frames may get captured.
	// The good thing is such frames are quite small compared to the regular frames.
	// For example a VGA (640x480) webcam frame is normally >= 8kByte large,
	// corrupted frames are smaller.
	if (buf->bytesused < dev->min_frame_size) {
		_D_LOG_DEBUG("Dropped too small frame, assuming it was broken: buffer=%u, bytesused=%u",
			buf->index, buf->bytesused);
		return false;
	}

	// Workaround for truncated JPEG frames:
	// Some inexpensive CCTV-style USB webcams such as the ELP-USB100W03M send
	// large amounts of these frames when using MJPEG streams. Checks that the
	// buffer ends with either the JPEG end of image marker (0xFFD9), the last
	// marker byte plus a padding byte (0xD900), or just padding bytes (0x0000)
	// A more sophisticated method would scan for the end of image marker, but
	// that takes precious CPU cycles and this should be good enough for most
	// cases.
	if (us_is_jpeg(dev->run->format)) {
		if (buf->bytesused < 125) {
			// https://stackoverflow.com/questions/2253404/what-is-the-smallest-valid-jpeg-file-size-in-bytes
			_D_LOG_DEBUG("Discarding invalid frame, too small to be a valid JPEG: bytesused=%u", buf->bytesused);
			return false;
		}

		const u8 *const end_ptr = data + buf->bytesused;
		const u8 *const eoi_ptr = end_ptr - 2;
		const u16 eoi_marker = (((u16)(eoi_ptr[0]) << 8) | eoi_ptr[1]);
		if (eoi_marker != 0xFFD9 && eoi_marker != 0xD900 && eoi_marker != 0x0000) {
			_D_LOG_DEBUG("Discarding truncated JPEG frame: eoi_marker=0x%04x, bytesused=%u", eoi_marker, buf->bytesused);
			return false;
		}
	}

	return true;
}

static int _device_open_check_cap(us_device_s *dev) {
	us_device_runtime_s *const run = dev->run;

	struct v4l2_capability cap = {0};
	_D_LOG_DEBUG("Querying device capabilities ...");
	if (us_xioctl(run->fd, VIDIOC_QUERYCAP, &cap) < 0) {
		_D_LOG_PERROR("Can't query device capabilities");
		return -1;
	}

	if (cap.capabilities & V4L2_CAP_VIDEO_CAPTURE) {
		run->capture_type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		run->capture_mplane = false;
		_D_LOG_INFO("Using capture type: single-planar");
	} else if (cap.capabilities & V4L2_CAP_VIDEO_CAPTURE_MPLANE) {
		run->capture_type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
		run->capture_mplane = true;
		_D_LOG_INFO("Using capture type: multi-planar");
	} else {
		_D_LOG_ERROR("Video capture is not supported by device");
		return -1;
	}

	if (!(cap.capabilities & V4L2_CAP_STREAMING)) {
		_D_LOG_ERROR("Device doesn't support streaming IO");
		return -1;
	}

	if (!run->capture_mplane) {
		int input = dev->input; // Needs a pointer to int for ioctl()
		_D_LOG_INFO("Using input channel: %d", input);
		if (us_xioctl(run->fd, VIDIOC_S_INPUT, &input) < 0) {
			_D_LOG_ERROR("Can't set input channel");
			return -1;
		}
	}

	if (dev->standard != V4L2_STD_UNKNOWN) {
		_D_LOG_INFO("Using TV standard: %s", _standard_to_string(dev->standard));
		if (us_xioctl(run->fd, VIDIOC_S_STD, &dev->standard) < 0) {
			_D_LOG_ERROR("Can't set video standard");
			return -1;
		}
	} else {
		_D_LOG_DEBUG("Using TV standard: DEFAULT");
	}
	return 0;
}

static int _device_open_dv_timings(us_device_s *dev) {
	_device_apply_resolution(dev, dev->width, dev->height, dev->run->hz);
	if (dev->dv_timings) {
		_D_LOG_DEBUG("Using DV-timings");

		if (_device_apply_dv_timings(dev) < 0) {
			return -1;
		}

		struct v4l2_event_subscription sub = {.type = V4L2_EVENT_SOURCE_CHANGE};
		_D_LOG_DEBUG("Subscribing to DV-timings events ...")
		if (us_xioctl(dev->run->fd, VIDIOC_SUBSCRIBE_EVENT, &sub) < 0) {
			_D_LOG_PERROR("Can't subscribe to DV-timings events");
			return -1;
		}
	}
	return 0;
}

static int _device_apply_dv_timings(us_device_s *dev) {
	us_device_runtime_s *const run = dev->run; // cppcheck-suppress constVariablePointer

	struct v4l2_dv_timings dv = {0};

	_D_LOG_DEBUG("Calling us_xioctl(VIDIOC_QUERY_DV_TIMINGS) ...");
	if (us_xioctl(run->fd, VIDIOC_QUERY_DV_TIMINGS, &dv) == 0) {
		float hz = 0;
		if (dv.type == V4L2_DV_BT_656_1120) {
			// See v4l2_print_dv_timings() in the kernel
			const uint htot = V4L2_DV_BT_FRAME_WIDTH(&dv.bt);
			const uint vtot = V4L2_DV_BT_FRAME_HEIGHT(&dv.bt) / (dv.bt.interlaced ? 2 : 1);
			const uint fps = ((htot * vtot) > 0 ? ((100 * (u64)dv.bt.pixelclock)) / (htot * vtot) : 0);
			hz = (fps / 100) + (fps % 100) / 100.0;
			_D_LOG_INFO("Got new DV-timings: %ux%u%s%.02f, pixclk=%llu, vsync=%u, hsync=%u",
				dv.bt.width, dv.bt.height, (dv.bt.interlaced ? "i" : "p"), hz,
				(ull)dv.bt.pixelclock, dv.bt.vsync, dv.bt.hsync); // See #11 about %llu
		} else {
			_D_LOG_INFO("Got new DV-timings: %ux%u, pixclk=%llu, vsync=%u, hsync=%u",
				dv.bt.width, dv.bt.height,
				(ull)dv.bt.pixelclock, dv.bt.vsync, dv.bt.hsync);
		}

		_D_LOG_DEBUG("Calling us_xioctl(VIDIOC_S_DV_TIMINGS) ...");
		if (us_xioctl(run->fd, VIDIOC_S_DV_TIMINGS, &dv) < 0) {
			_D_LOG_PERROR("Failed to set DV-timings");
			return -1;
		}

		if (_device_apply_resolution(dev, dv.bt.width, dv.bt.height, hz) < 0) {
			return -1;
		}

	} else {
		_D_LOG_DEBUG("Calling us_xioctl(VIDIOC_QUERYSTD) ...");
		if (us_xioctl(run->fd, VIDIOC_QUERYSTD, &dev->standard) == 0) {
			_D_LOG_INFO("Applying the new VIDIOC_S_STD: %s ...", _standard_to_string(dev->standard));
			if (us_xioctl(run->fd, VIDIOC_S_STD, &dev->standard) < 0) {
				_D_LOG_PERROR("Can't set video standard");
				return -1;
			}
		}
	}
	return 0;
}

static int _device_open_format(us_device_s *dev, bool first) {
	us_device_runtime_s *const run = dev->run;

	const uint stride = us_align_size(run->width, 32) << 1;

	struct v4l2_format fmt = {0};
	fmt.type = run->capture_type;
	if (run->capture_mplane) {
		fmt.fmt.pix_mp.width = run->width;
		fmt.fmt.pix_mp.height = run->height;
		fmt.fmt.pix_mp.pixelformat = dev->format;
		fmt.fmt.pix_mp.field = V4L2_FIELD_ANY;
		fmt.fmt.pix_mp.flags = 0;
		fmt.fmt.pix_mp.num_planes = 1;
	} else {
		fmt.fmt.pix.width = run->width;
		fmt.fmt.pix.height = run->height;
		fmt.fmt.pix.pixelformat = dev->format;
		fmt.fmt.pix.field = V4L2_FIELD_ANY;
		fmt.fmt.pix.bytesperline = stride;
	}

	// Set format
	_D_LOG_DEBUG("Probing device format=%s, stride=%u, resolution=%ux%u ...",
		_format_to_string_supported(dev->format), stride, run->width, run->height);
	if (us_xioctl(run->fd, VIDIOC_S_FMT, &fmt) < 0) {
		_D_LOG_PERROR("Can't set device format");
		return -1;
	}

	if (fmt.type != run->capture_type) {
		_D_LOG_ERROR("Capture format mismatch, please report to the developer");
		return -1;
	}

#	define FMT(x_next)	(run->capture_mplane ? fmt.fmt.pix_mp.x_next : fmt.fmt.pix.x_next)
#	define FMTS(x_next)	(run->capture_mplane ? fmt.fmt.pix_mp.plane_fmt[0].x_next : fmt.fmt.pix.x_next)

	// Check resolution
	bool retry = false;
	if (FMT(width) != run->width || FMT(height) != run->height) {
		_D_LOG_ERROR("Requested resolution=%ux%u is unavailable", run->width, run->height);
		retry = true;
	}
	if (_device_apply_resolution(dev, FMT(width), FMT(height), run->hz) < 0) {
		return -1;
	}
	if (first && retry) {
		return _device_open_format(dev, false);
	}
	_D_LOG_INFO("Using resolution: %ux%u", run->width, run->height);

	// Check format
	if (FMT(pixelformat) != dev->format) {
		_D_LOG_ERROR("Could not obtain the requested format=%s; driver gave us %s",
			_format_to_string_supported(dev->format),
			_format_to_string_supported(FMT(pixelformat)));

		char *format_str;
		if ((format_str = (char *)_format_to_string_nullable(FMT(pixelformat))) != NULL) {
			_D_LOG_INFO("Falling back to format=%s", format_str);
		} else {
			char fourcc_str[8];
			_D_LOG_ERROR("Unsupported format=%s (fourcc)",
				us_fourcc_to_string(FMT(pixelformat), fourcc_str, 8));
			return -1;
		}
	}

	run->format = FMT(pixelformat);
	_D_LOG_INFO("Using format: %s", _format_to_string_supported(run->format));


	run->stride = FMTS(bytesperline);
	run->raw_size = FMTS(sizeimage); // Only for userptr

#	undef FMTS
#	undef FMT

	return 0;
}

static void _device_open_hw_fps(us_device_s *dev) {
	us_device_runtime_s *const run = dev->run;

	run->hw_fps = 0;

	struct v4l2_streamparm setfps = {.type = run->capture_type};
	_D_LOG_DEBUG("Querying HW FPS ...");
	if (us_xioctl(run->fd, VIDIOC_G_PARM, &setfps) < 0) {
		if (errno == ENOTTY) { // Quiet message for TC358743
			_D_LOG_INFO("Querying HW FPS changing is not supported");
		} else {
			_D_LOG_PERROR("Can't query HW FPS changing");
		}
		return;
	}

	if (!(setfps.parm.capture.capability & V4L2_CAP_TIMEPERFRAME)) {
		_D_LOG_INFO("Changing HW FPS is not supported");
		return;
	}

#	define SETFPS_TPF(x_next) setfps.parm.capture.timeperframe.x_next

	US_MEMSET_ZERO(setfps);
	setfps.type = run->capture_type;
	SETFPS_TPF(numerator) = 1;
	SETFPS_TPF(denominator) = (dev->desired_fps == 0 ? 255 : dev->desired_fps);

	if (us_xioctl(run->fd, VIDIOC_S_PARM, &setfps) < 0) {
		_D_LOG_PERROR("Can't set HW FPS");
		return;
	}

	if (SETFPS_TPF(numerator) != 1) {
		_D_LOG_ERROR("Invalid HW FPS numerator: %u != 1", SETFPS_TPF(numerator));
		return;
	}

	if (SETFPS_TPF(denominator) == 0) { // Не знаю, бывает ли так, но пускай на всякий случай
		_D_LOG_ERROR("Invalid HW FPS denominator: 0");
		return;
	}

	run->hw_fps = SETFPS_TPF(denominator);
	if (dev->desired_fps != run->hw_fps) {
		_D_LOG_INFO("Using HW FPS: %u -> %u (coerced)", dev->desired_fps, run->hw_fps);
	} else {
		_D_LOG_INFO("Using HW FPS: %u", run->hw_fps);
	}

#	undef SETFPS_TPF
}

static void _device_open_jpeg_quality(us_device_s *dev) {
	us_device_runtime_s *const run = dev->run;
	uint quality = 0;
	if (us_is_jpeg(run->format)) {
		struct v4l2_jpegcompression comp = {0};
		if (us_xioctl(run->fd, VIDIOC_G_JPEGCOMP, &comp) < 0) {
			_D_LOG_ERROR("Device doesn't support setting of HW encoding quality parameters");
		} else {
			comp.quality = dev->jpeg_quality;
			if (us_xioctl(run->fd, VIDIOC_S_JPEGCOMP, &comp) < 0) {
				_D_LOG_ERROR("Can't change MJPEG quality for JPEG source with HW pass-through encoder");
			} else {
				quality = dev->jpeg_quality;
			}
		}
	}
	run->jpeg_quality = quality;
}

static int _device_open_io_method(us_device_s *dev) {
	_D_LOG_INFO("Using IO method: %s", _io_method_to_string_supported(dev->io_method));
	switch (dev->io_method) {
		case V4L2_MEMORY_MMAP: return _device_open_io_method_mmap(dev);
		case V4L2_MEMORY_USERPTR: return _device_open_io_method_userptr(dev);
		default: assert(0 && "Unsupported IO method");
	}
	return -1;
}

static int _device_open_io_method_mmap(us_device_s *dev) {
	us_device_runtime_s *const run = dev->run;

	struct v4l2_requestbuffers req = {
		.count = dev->n_bufs,
		.type = run->capture_type,
		.memory = V4L2_MEMORY_MMAP,
	};
	_D_LOG_DEBUG("Requesting %u device buffers for MMAP ...", req.count);
	if (us_xioctl(run->fd, VIDIOC_REQBUFS, &req) < 0) {
		_D_LOG_PERROR("Device '%s' doesn't support MMAP method", dev->path);
		return -1;
	}

	if (req.count < 1) {
		_D_LOG_ERROR("Insufficient buffer memory: %u", req.count);
		return -1;
	} else {
		_D_LOG_INFO("Requested %u device buffers, got %u", dev->n_bufs, req.count);
	}

	_D_LOG_DEBUG("Allocating device buffers ...");

	US_CALLOC(run->hw_bufs, req.count);

	for (run->n_bufs = 0; run->n_bufs < req.count; ++run->n_bufs) {
		struct v4l2_buffer buf = {0};
		struct v4l2_plane planes[VIDEO_MAX_PLANES] = {0};
		buf.type = run->capture_type;
		buf.memory = V4L2_MEMORY_MMAP;
		buf.index = run->n_bufs;
		if (run->capture_mplane) {
			buf.m.planes = planes;
			buf.length = VIDEO_MAX_PLANES;
		}

		_D_LOG_DEBUG("Calling us_xioctl(VIDIOC_QUERYBUF) for device buffer=%u ...", run->n_bufs);
		if (us_xioctl(run->fd, VIDIOC_QUERYBUF, &buf) < 0) {
			_D_LOG_PERROR("Can't VIDIOC_QUERYBUF");
			return -1;
		}

		us_hw_buffer_s *hw = &run->hw_bufs[run->n_bufs];
		const uz buf_size = (run->capture_mplane ? buf.m.planes[0].length : buf.length);
		const off_t buf_offset = (run->capture_mplane ? buf.m.planes[0].m.mem_offset : buf.m.offset);

		_D_LOG_DEBUG("Mapping device buffer=%u ...", run->n_bufs);
		if ((hw->raw.data = mmap(
			NULL, buf_size,
			PROT_READ | PROT_WRITE, MAP_SHARED,
			run->fd, buf_offset
		)) == MAP_FAILED) {
			_D_LOG_PERROR("Can't map device buffer=%u", run->n_bufs);
			return -1;
		}
		assert(hw->raw.data != NULL);
		hw->raw.allocated = buf_size;

		if (run->capture_mplane) {
			US_CALLOC(hw->buf.m.planes, VIDEO_MAX_PLANES);
		}

		hw->dma_fd = -1;
	}
	return 0;
}

static int _device_open_io_method_userptr(us_device_s *dev) {
	us_device_runtime_s *const run = dev->run;

	struct v4l2_requestbuffers req = {
		.count = dev->n_bufs,
		.type = run->capture_type,
		.memory = V4L2_MEMORY_USERPTR,
	};
	_D_LOG_DEBUG("Requesting %u device buffers for USERPTR ...", req.count);
	if (us_xioctl(run->fd, VIDIOC_REQBUFS, &req) < 0) {
		_D_LOG_PERROR("Device '%s' doesn't support USERPTR method", dev->path);
		return -1;
	}

	if (req.count < 1) {
		_D_LOG_ERROR("Insufficient buffer memory: %u", req.count);
		return -1;
	} else {
		_D_LOG_INFO("Requested %u device buffers, got %u", dev->n_bufs, req.count);
	}

	_D_LOG_DEBUG("Allocating device buffers ...");

	US_CALLOC(run->hw_bufs, req.count);

	const uint page_size = getpagesize();
	const uint buf_size = us_align_size(run->raw_size, page_size);

	for (run->n_bufs = 0; run->n_bufs < req.count; ++run->n_bufs) {
		us_hw_buffer_s *hw = &run->hw_bufs[run->n_bufs];
		assert((hw->raw.data = aligned_alloc(page_size, buf_size)) != NULL);
		memset(hw->raw.data, 0, buf_size);
		hw->raw.allocated = buf_size;
		if (run->capture_mplane) {
			US_CALLOC(hw->buf.m.planes, VIDEO_MAX_PLANES);
		}
	}
	return 0;
}

static int _device_open_queue_buffers(us_device_s *dev) {
	us_device_runtime_s *const run = dev->run;

	for (uint index = 0; index < run->n_bufs; ++index) {
		struct v4l2_buffer buf = {0};
		struct v4l2_plane planes[VIDEO_MAX_PLANES] = {0};
		buf.type = run->capture_type;
		buf.memory = dev->io_method;
		buf.index = index;
		if (run->capture_mplane) {
			buf.m.planes = planes;
			buf.length = 1;
		}
		
		if (dev->io_method == V4L2_MEMORY_USERPTR) {
			// I am not sure, may be this is incorrect for mplane device, 
			// but i don't have one which supports V4L2_MEMORY_USERPTR
			buf.m.userptr = (unsigned long)run->hw_bufs[index].raw.data;
			buf.length = run->hw_bufs[index].raw.allocated;
		}

		_D_LOG_DEBUG("Calling us_xioctl(VIDIOC_QBUF) for buffer=%u ...", index);
		if (us_xioctl(run->fd, VIDIOC_QBUF, &buf) < 0) {
			_D_LOG_PERROR("Can't VIDIOC_QBUF");
			return -1;
		}
	}
	return 0;
}

static int _device_open_export_to_dma(us_device_s *dev) {
	us_device_runtime_s *const run = dev->run;

	for (uint index = 0; index < run->n_bufs; ++index) {
		struct v4l2_exportbuffer exp = {
			.type = run->capture_type,
			.index = index,
		};
		_D_LOG_DEBUG("Exporting device buffer=%u to DMA ...", index);
		if (us_xioctl(run->fd, VIDIOC_EXPBUF, &exp) < 0) {
			_D_LOG_PERROR("Can't export device buffer=%u to DMA", index);
			goto error;
		}
		run->hw_bufs[index].dma_fd = exp.fd;
	}
	return 0;

error:
	for (uint index = 0; index < run->n_bufs; ++index) {
		US_CLOSE_FD(run->hw_bufs[index].dma_fd);
	}
	return -1;
}

static int _device_apply_resolution(us_device_s *dev, uint width, uint height, float hz) {
	// Тут VIDEO_MIN_* не используются из-за странностей минимального разрешения при отсутствии сигнала
	// у некоторых устройств, например TC358743
	if (
		width == 0 || width > US_VIDEO_MAX_WIDTH
		|| height == 0 || height > US_VIDEO_MAX_HEIGHT
	) {
		_D_LOG_ERROR("Requested forbidden resolution=%ux%u: min=1x1, max=%ux%u",
			width, height, US_VIDEO_MAX_WIDTH, US_VIDEO_MAX_HEIGHT);
		return -1;
	}
	dev->run->width = width;
	dev->run->height = height;
	dev->run->hz = hz;
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
	const char *name, uint cid, bool quiet) {

	// cppcheck-suppress redundantPointerOp
	US_MEMSET_ZERO(*query);
	query->id = cid;

	if (us_xioctl(dev->run->fd, VIDIOC_QUERYCTRL, query) < 0 || query->flags & V4L2_CTRL_FLAG_DISABLED) {
		if (!quiet) {
			_D_LOG_ERROR("Changing control %s is unsupported", name);
		}
		return -1;
	}
	return 0;
}

static void _device_set_control(
	us_device_s *dev, const struct v4l2_queryctrl *query,
	const char *name, uint cid, int value, bool quiet) {

	if (value < query->minimum || value > query->maximum || value % query->step != 0) {
		if (!quiet) {
			_D_LOG_ERROR("Invalid value %d of control %s: min=%d, max=%d, default=%d, step=%u",
				value, name, query->minimum, query->maximum, query->default_value, query->step);
		}
		return;
	}

	struct v4l2_control ctl = {
		.id = cid,
		.value = value,
	};
	if (us_xioctl(dev->run->fd, VIDIOC_S_CTRL, &ctl) < 0) {
		if (!quiet) {
			_D_LOG_PERROR("Can't set control %s", name);
		}
	} else if (!quiet) {
		_D_LOG_INFO("Applying control %s: %d", name, ctl.value);
	}
}

static const char *_format_to_string_nullable(uint format) {
	US_ARRAY_ITERATE(_FORMATS, 0, item, {
		if (item->format == format) {
			return item->name;
		}
	});
	return NULL;
}

static const char *_format_to_string_supported(uint format) {
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
