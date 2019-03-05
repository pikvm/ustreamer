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


#include <stdlib.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <fcntl.h>
#include <assert.h>

#include <sys/mman.h>
#include <linux/videodev2.h>

#include "tools.h"
#include "logging.h"
#include "xioctl.h"
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
	{"JPEG",	V4L2_PIX_FMT_MJPEG},
	{"JPEG",	V4L2_PIX_FMT_JPEG},
};


static int _device_open_check_cap(struct device_t *dev);
static int _device_open_dv_timings(struct device_t *dev);
static int _device_apply_dv_timings(struct device_t *dev);
static int _device_open_format(struct device_t *dev);
static void _device_open_set_image_settings(struct device_t *dev);
static void _device_open_alloc_picbufs(struct device_t *dev);
static int _device_open_mmap(struct device_t *dev);
static int _device_open_queue_buffers(struct device_t *dev);
static int _device_apply_resolution(struct device_t *dev, unsigned width, unsigned height);

static const char *_format_to_string_fourcc(char *buf, size_t size, unsigned format);
static const char *_format_to_string_nullable(unsigned format);
static const char *_format_to_string_supported(unsigned format);
static const char *_standard_to_string(v4l2_std_id standard);


struct device_t *device_init() {
	struct image_settings_t *img;
	struct device_runtime_t *run;
	struct device_t *dev;

	A_CALLOC(img, 1);

	A_CALLOC(run, 1);
	run->fd = -1;

	A_CALLOC(dev, 1);
	dev->path = "/dev/video0";
	dev->width = 640;
	dev->height = 480;
	dev->format = V4L2_PIX_FMT_YUYV;
	dev->standard = V4L2_STD_UNKNOWN;
	dev->n_buffers = max_u(sysconf(_SC_NPROCESSORS_ONLN), 1) + 1;
	dev->n_workers = dev->n_buffers;
	dev->timeout = 1;
	dev->error_delay = 1;
	dev->img = img;
	dev->run = run;
	return dev;
}

void device_destroy(struct device_t *dev) {
	free(dev->run);
	free(dev->img);
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

int device_open(struct device_t *dev) {
	if ((dev->run->fd = open(dev->path, O_RDWR|O_NONBLOCK)) < 0) {
		LOG_PERROR("Can't open device");
		goto error;
	}
	LOG_INFO("Device fd=%d opened", dev->run->fd);

	if (_device_open_check_cap(dev) < 0) {
		goto error;
	}
	if (_device_open_dv_timings(dev) < 0) {
		goto error;
	}
	if (_device_open_format(dev) < 0) {
		goto error;
	}
	_device_open_set_image_settings(dev);
	if (_device_open_mmap(dev) < 0) {
		goto error;
	}
	if (_device_open_queue_buffers(dev) < 0) {
		goto error;
	}
	_device_open_alloc_picbufs(dev);

	LOG_DEBUG("Device fd=%d initialized", dev->run->fd);
	return 0;

	error:
		device_close(dev);
		return -1;
}

void device_close(struct device_t *dev) {
	if (dev->run->pictures) {
		LOG_DEBUG("Releasing picture buffers ...");
		for (unsigned index = 0; index < dev->run->n_buffers && dev->run->pictures[index].data; ++index) {
			free(dev->run->pictures[index].data);
			dev->run->pictures[index].data = NULL;
		}
		free(dev->run->pictures);
		dev->run->pictures = NULL;
	}

	if (dev->run->hw_buffers) {
		LOG_DEBUG("Unmapping HW buffers ...");
		for (unsigned index = 0; index < dev->run->n_buffers; ++index) {
			if (dev->run->hw_buffers[index].start != MAP_FAILED) {
				if (munmap(dev->run->hw_buffers[index].start, dev->run->hw_buffers[index].length) < 0) {
					LOG_PERROR("Can't unmap device buffer %u", index);
				}
			}
		}
		dev->run->n_buffers = 0;
		free(dev->run->hw_buffers);
		dev->run->hw_buffers = NULL;
	}

	if (dev->run->fd >= 0) {
		LOG_DEBUG("Closing device ...");
		if (close(dev->run->fd) < 0) {
			LOG_PERROR("Can't close device fd=%d", dev->run->fd);
		} else {
			LOG_INFO("Device fd=%d closed", dev->run->fd);
		}
		dev->run->fd = -1;
	}
}

static int _device_open_check_cap(struct device_t *dev) {
	struct v4l2_capability cap;
	int input = dev->input; // Needs pointer to int for ioctl()

	MEMSET_ZERO(cap);

	LOG_DEBUG("Calling ioctl(VIDIOC_QUERYCAP) ...");
	if (xioctl(dev->run->fd, VIDIOC_QUERYCAP, &cap) < 0) {
		LOG_PERROR("Can't query device (VIDIOC_QUERYCAP)");
		return -1;
	}

	if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
		LOG_ERROR("Video capture not supported by our device");
		return -1;
	}

	if (!(cap.capabilities & V4L2_CAP_STREAMING)) {
		LOG_ERROR("Device does not support streaming IO");
		return -1;
	}

	LOG_INFO("Using input channel: %d", input);
	if (xioctl(dev->run->fd, VIDIOC_S_INPUT, &input) < 0) {
		LOG_ERROR("Can't set input channel");
		return -1;
	}

	if (dev->standard != V4L2_STD_UNKNOWN) {
		LOG_INFO("Using TV standard: %s", _standard_to_string(dev->standard));
		if (xioctl(dev->run->fd, VIDIOC_S_STD, &dev->standard) < 0) {
			LOG_ERROR("Can't set video standard");
			return -1;
		}
	} else {
		LOG_INFO("Using TV standard: DEFAULT");
	}
	return 0;
}

static int _device_open_dv_timings(struct device_t *dev) {
	_device_apply_resolution(dev, dev->width, dev->height);
	if (dev->dv_timings) {
		LOG_DEBUG("Using DV-timings");

		if (_device_apply_dv_timings(dev) < 0) {
			return -1;
		}

		struct v4l2_event_subscription sub;

		MEMSET_ZERO(sub);
		sub.type = V4L2_EVENT_SOURCE_CHANGE;

		LOG_DEBUG("Calling ioctl(VIDIOC_SUBSCRIBE_EVENT) ...");
		if (xioctl(dev->run->fd, VIDIOC_SUBSCRIBE_EVENT, &sub) < 0) {
			LOG_PERROR("Can't subscribe to V4L2_EVENT_SOURCE_CHANGE");
			return -1;
		}
	}
	return 0;
}

static int _device_apply_dv_timings(struct device_t *dev) {
	struct v4l2_dv_timings dv_timings;

	MEMSET_ZERO(dv_timings);

	LOG_DEBUG("Calling ioctl(VIDIOC_QUERY_DV_TIMINGS) ...");
	if (xioctl(dev->run->fd, VIDIOC_QUERY_DV_TIMINGS, &dv_timings) == 0) {
		LOG_INFO(
			"Got new DV timings: resolution=%ux%u; pixclk=%llu",
			dv_timings.bt.width,
			dv_timings.bt.height,
			dv_timings.bt.pixelclock
		);

		LOG_DEBUG("Calling ioctl(VIDIOC_S_DV_TIMINGS) ...");
		if (xioctl(dev->run->fd, VIDIOC_S_DV_TIMINGS, &dv_timings) < 0) {
			LOG_PERROR("Failed to set DV timings");
			return -1;
		}

		if (_device_apply_resolution(dev, dv_timings.bt.width, dv_timings.bt.height) < 0) {
			return -1;
		}

	} else {
		LOG_DEBUG("Calling ioctl(VIDIOC_QUERYSTD) ...");
		if (xioctl(dev->run->fd, VIDIOC_QUERYSTD, &dev->standard) == 0) {
			LOG_INFO("Applying the new VIDIOC_S_STD: %s ...", _standard_to_string(dev->standard));
			if (xioctl(dev->run->fd, VIDIOC_S_STD, &dev->standard) < 0) {
				LOG_PERROR("Can't set video standard");
				return -1;
			}
		}
	}
	return 0;
}

static int _device_open_format(struct device_t *dev) {
	struct v4l2_format fmt;

	MEMSET_ZERO(fmt);
	fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	fmt.fmt.pix.width = dev->run->width;
	fmt.fmt.pix.height = dev->run->height;
	fmt.fmt.pix.pixelformat = dev->format;
	fmt.fmt.pix.field = V4L2_FIELD_ANY;

	// Set format
	LOG_DEBUG("Calling ioctl(VIDIOC_S_FMT) ...");
	if (xioctl(dev->run->fd, VIDIOC_S_FMT, &fmt) < 0) {
		LOG_PERROR(
			"Unable to set pixelformat=%s; resolution=%ux%u",
			_format_to_string_supported(dev->format),
			dev->run->width,
			dev->run->height
		);
		return -1;
	}

	// Check resolution
	if (fmt.fmt.pix.width != dev->run->width || fmt.fmt.pix.height != dev->run->height) {
		LOG_ERROR("Requested resolution=%ux%u is unavailable", dev->run->width, dev->run->height);
	}
	if (_device_apply_resolution(dev, fmt.fmt.pix.width, fmt.fmt.pix.height) < 0) {
		return -1;
	}
	LOG_INFO("Using resolution: %ux%u", dev->run->width, dev->run->height);

	// Check format
	if (fmt.fmt.pix.pixelformat != dev->format) {
		char format_obtained_str[8];
		char *format_str_nullable;

		LOG_ERROR(
			"Could not obtain the requested pixelformat=%s; driver gave us %s",
			_format_to_string_supported(dev->format),
			_format_to_string_supported(fmt.fmt.pix.pixelformat)
		);

		if ((format_str_nullable = (char *)_format_to_string_nullable(fmt.fmt.pix.pixelformat)) != NULL) {
			LOG_INFO(
				"Falling back to %s mode (consider using '--format=%s' option)",
				format_str_nullable,
				format_str_nullable
			);
		} else {
			LOG_ERROR(
				"Unsupported pixelformat=%s (fourcc)",
				_format_to_string_fourcc(format_obtained_str, 8, fmt.fmt.pix.pixelformat)
			);
			return -1;
		}
	}

	dev->run->format = fmt.fmt.pix.pixelformat;
	LOG_INFO("Using pixelformat: %s", _format_to_string_supported(dev->run->format));
	return 0;
}

static void _device_open_set_image_settings(struct device_t *dev) {
	struct v4l2_queryctrl query;
	struct v4l2_control ctl;

#	define SET_CID(_cid, _dest) { \
			MEMSET_ZERO(query); query.id = _cid; \
			if (xioctl(dev->run->fd, VIDIOC_QUERYCTRL, &query) != 0) { \
				LOG_INFO("Changing image " #_dest " is unsupported"); \
			} else { \
				MEMSET_ZERO(ctl); ctl.id = _cid; ctl.value = (int)dev->img->_dest; \
				if (xioctl(dev->run->fd, VIDIOC_S_CTRL, &ctl) < 0) { LOG_PERROR("Can't set image " #_dest); } \
				else { LOG_INFO("Using image " #_dest ": %d", ctl.value); } \
			} \
		}

#	define SET_CID_MANUAL(_cid, _dest) { \
			if (dev->img->_dest##_set) { SET_CID(_cid, _dest); } \
		}

#	define SET_CID_AUTO(_cid_auto, _cid_manual, _dest) { \
			if (dev->img->_dest##_set) { \
				SET_CID(_cid_auto, _dest##_auto); \
				if (!dev->img->_dest##_auto) { SET_CID(_cid_manual, _dest); } \
			} \
		}

	SET_CID_AUTO	(V4L2_CID_AUTOBRIGHTNESS,		V4L2_CID_BRIGHTNESS,				brightness);
	SET_CID_MANUAL	(								V4L2_CID_CONTRAST,					contrast);
	SET_CID_MANUAL	(								V4L2_CID_SATURATION,				saturation);
	SET_CID_AUTO	(V4L2_CID_HUE_AUTO,				V4L2_CID_HUE,						hue);
	SET_CID_MANUAL	(								V4L2_CID_GAMMA,						gamma);
	SET_CID_MANUAL	(								V4L2_CID_SHARPNESS,					sharpness);
	SET_CID_MANUAL	(								V4L2_CID_BACKLIGHT_COMPENSATION,	backlight_compensation);
	SET_CID_AUTO	(V4L2_CID_AUTO_WHITE_BALANCE,	V4L2_CID_WHITE_BALANCE_TEMPERATURE,	white_balance);
	SET_CID_AUTO	(V4L2_CID_AUTOGAIN,				V4L2_CID_GAIN,						gain);

#	undef SET_CID_AUTO
#	undef SET_CID_MANUAL
#	undef SET_CID
}

static int _device_open_mmap(struct device_t *dev) {
	struct v4l2_requestbuffers req;

	MEMSET_ZERO(req);
	req.count = dev->n_buffers;
	req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	req.memory = V4L2_MEMORY_MMAP;

	LOG_DEBUG("Calling ioctl(VIDIOC_REQBUFS) ...");
	if (xioctl(dev->run->fd, VIDIOC_REQBUFS, &req)) {
		LOG_PERROR("Device '%s' doesn't support memory mapping", dev->path);
		return -1;
	}

	if (req.count < 1) {
		LOG_ERROR("Insufficient buffer memory: %u", req.count);
		return -1;
	} else {
		LOG_INFO("Requested %u HW buffers, got %u", dev->n_buffers, req.count);
	}

	LOG_DEBUG("Allocating HW buffers ...");

	A_CALLOC(dev->run->hw_buffers, req.count);
	for (dev->run->n_buffers = 0; dev->run->n_buffers < req.count; ++dev->run->n_buffers) {
		struct v4l2_buffer buf_info;

		MEMSET_ZERO(buf_info);
		buf_info.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buf_info.memory = V4L2_MEMORY_MMAP;
		buf_info.index = dev->run->n_buffers;

		LOG_DEBUG("Calling ioctl(VIDIOC_QUERYBUF) for device buffer %u ...", dev->run->n_buffers);
		if (xioctl(dev->run->fd, VIDIOC_QUERYBUF, &buf_info) < 0) {
			LOG_PERROR("Can't VIDIOC_QUERYBUF");
			return -1;
		}

		LOG_DEBUG("Mapping device buffer %u ...", dev->run->n_buffers);
		dev->run->hw_buffers[dev->run->n_buffers].length = buf_info.length;
		dev->run->hw_buffers[dev->run->n_buffers].start = mmap(NULL, buf_info.length, PROT_READ|PROT_WRITE, MAP_SHARED, dev->run->fd, buf_info.m.offset);
		if (dev->run->hw_buffers[dev->run->n_buffers].start == MAP_FAILED) {
			LOG_PERROR("Can't map device buffer %u", dev->run->n_buffers);
			return -1;
		}
	}
	return 0;
}

static int _device_open_queue_buffers(struct device_t *dev) {
	for (unsigned index = 0; index < dev->run->n_buffers; ++index) {
		struct v4l2_buffer buf_info;

		MEMSET_ZERO(buf_info);
		buf_info.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buf_info.memory = V4L2_MEMORY_MMAP;
		buf_info.index = index;

		LOG_DEBUG("Calling ioctl(VIDIOC_QBUF) for buffer %u ...", index);
		if (xioctl(dev->run->fd, VIDIOC_QBUF, &buf_info) < 0) {
			LOG_PERROR("Can't VIDIOC_QBUF");
			return -1;
		}
	}
	return 0;
}

static void _device_open_alloc_picbufs(struct device_t *dev) {
	LOG_DEBUG("Allocating picture buffers ...");
	A_CALLOC(dev->run->pictures, dev->run->n_buffers);

	dev->run->max_picture_size = ((dev->run->width * dev->run->height) << 1) * 2;
	for (unsigned index = 0; index < dev->run->n_buffers; ++index) {
		LOG_DEBUG("Allocating picture buffer %u sized %lu bytes... ", index, dev->run->max_picture_size);
		A_CALLOC(dev->run->pictures[index].data, dev->run->max_picture_size);
		dev->run->pictures[index].allocated = dev->run->max_picture_size;
	}
}

static int _device_apply_resolution(struct device_t *dev, unsigned width, unsigned height) {
	// Тут VIDEO_MIN_* не используются из-за странностей минимального разрешения при отсутствии сигнала
	// у некоторых устройств, например Auvidea B101
	if (
		width == 0 || width > VIDEO_MAX_WIDTH
		|| height == 0 || height > VIDEO_MAX_HEIGHT
	) {
		LOG_ERROR("Requested forbidden resolution=%ux%u: min=1x1; max=%ux%u",
			width, height, VIDEO_MAX_WIDTH, VIDEO_MAX_HEIGHT);
		return -1;
	}
	dev->run->width = width;
	dev->run->height = height;
	return 0;
}

static const char *_format_to_string_fourcc(char *buf, size_t size, unsigned format) {
	assert(size >= 8);
	buf[0] = format & 0x7F;
	buf[1] = (format >> 8) & 0x7F;
	buf[2] = (format >> 16) & 0x7F;
	buf[3] = (format >> 24) & 0x7F;
	if (format & (1 << 31)) {
		buf[4] = '-';
		buf[5] = 'B';
		buf[6] = 'E';
		buf[7] = '\0';
	} else {
		buf[4] = '\0';
	}
	return buf;
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
