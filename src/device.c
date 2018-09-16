#include <stdlib.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/param.h>
#include <linux/videodev2.h>

#include "tools.h"
#include "device.h"


static const char DEFAULT_DEVICE[] = "/dev/video0";

static const struct {
	const char *name;
	const v4l2_std_id standard;
} STANDARDS[] = {
	{"UNKNOWN",	V4L2_STD_UNKNOWN},
	{"PAL",		V4L2_STD_PAL},
	{"NTSC",	V4L2_STD_NTSC},
	{"SECAM",	V4L2_STD_SECAM}
};

static const struct {
	const char *name;
	const unsigned format;
} FORMATS[] = {
	{"YUYV",	V4L2_PIX_FMT_YUYV},
	{"UYVY",	V4L2_PIX_FMT_UYVY},
	{"RGB565",	V4L2_PIX_FMT_RGB565}
};


static int _device_open_check_cap(struct device_t *dev);
static int _device_open_dv_timings(struct device_t *dev);
static int _device_apply_dv_timings(struct device_t *dev);
static int _device_open_format(struct device_t *dev);
static void _device_open_alloc_picbufs(struct device_t *dev);
static int _device_open_mmap(struct device_t *dev);
static int _device_open_queue_buffers(struct device_t *dev);

static const char *_format_to_string_auto(char *buf, const size_t length, const unsigned format);
static const char *_format_to_string_null(const unsigned format);
static const char *_standard_to_string(const v4l2_std_id standard);


void device_init(struct device_t *dev, struct device_runtime_t *run) {
	LOG_DEBUG("Initializing a new device struct ...");

	memset(dev, 0, sizeof(struct device_t));
	memset(run, 0, sizeof(struct device_runtime_t));

	dev->path = (char *)DEFAULT_DEVICE;
	dev->width = 640;
	dev->height = 480;
	dev->format = V4L2_PIX_FMT_YUYV;
	dev->standard = V4L2_STD_UNKNOWN;
	dev->n_buffers = MAX(sysconf(_SC_NPROCESSORS_ONLN), 1);
	dev->jpeg_quality = 80;
	dev->timeout = 1;
	dev->error_timeout = 1;

	dev->run = run;
	dev->run->fd = -1;
	LOG_DEBUG("We have a clear device!");
}

int device_parse_format(const char *const str) {
	for (unsigned index = 0; index < sizeof(FORMATS) / sizeof(FORMATS[0]); ++index) {
		if (!strcasecmp(str, FORMATS[index].name)) {
			return FORMATS[index].format;
		}
	}
	return FORMAT_UNKNOWN;
}

v4l2_std_id device_parse_standard(const char *const str) {
	for (unsigned index = 1; index < sizeof(STANDARDS) / sizeof(STANDARDS[0]); ++index) {
		if (!strcasecmp(str, STANDARDS[index].name)) {
			return STANDARDS[index].standard;
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
		for (unsigned index = 0; index < dev->run->n_buffers && dev->run->pictures[index]; ++index) {
			free(dev->run->pictures[index]);
		}
		free(dev->run->pictures);
		dev->run->pictures = NULL;
	}

	if (dev->run->buffers) {
		LOG_DEBUG("Unmapping device buffers ...");
		for (unsigned index = 0; index < dev->run->n_buffers; ++index) {
			if (dev->run->buffers[index].start != MAP_FAILED) {
				if (munmap(dev->run->buffers[index].start, dev->run->buffers[index].length) < 0) {
					LOG_PERROR("Can't unmap device buffer %d", index);
				}
			}
		}
		dev->run->n_buffers = 0;
		free(dev->run->buffers);
		dev->run->buffers = NULL;
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

	if (dev->standard != V4L2_STD_UNKNOWN) {
		LOG_INFO("Using TV standard: %s", _standard_to_string(dev->standard));
		if (xioctl(dev->run->fd, VIDIOC_S_STD, &dev->standard) < 0) {
			LOG_PERROR("Can't set video standard");
			return -1;
		}
	} else {
		LOG_INFO("Using TV standard: DEFAULT");
	}
	return 0;
}

static int _device_open_dv_timings(struct device_t *dev) {
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

	} else {
		dev->run->width = dev->width;
		dev->run->height = dev->height;
	}
	return 0;
}

static int _device_apply_dv_timings(struct device_t *dev) {
	struct v4l2_dv_timings dv_timings;

	MEMSET_ZERO(dv_timings);

	LOG_DEBUG("Calling ioctl(VIDIOC_QUERY_DV_TIMINGS) ...");
	if (xioctl(dev->run->fd, VIDIOC_QUERY_DV_TIMINGS, &dv_timings) == 0) {
		LOG_INFO(
			"Got new DV timings: resolution=%dx%d; pixclk=%llu\n",
			dv_timings.bt.width,
			dv_timings.bt.height,
			dv_timings.bt.pixelclock
		);

		LOG_DEBUG("Calling ioctl(VIDIOC_S_DV_TIMINGS) ...");
		if (xioctl(dev->run->fd, VIDIOC_S_DV_TIMINGS, &dv_timings) < 0) {
			LOG_PERROR("Failed to set DV timings");
			return -1;
		}

		dev->run->width = dv_timings.bt.width;
		dev->run->height = dv_timings.bt.height;

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
		char format_str[8];

		LOG_PERROR(
			"Unable to set format=%s; resolution=%dx%d",
			_format_to_string_auto(format_str, 8, dev->format),
			dev->run->width,
			dev->run->height
		);
		return -1;
	}

	// Check resolution
	if (fmt.fmt.pix.width != dev->run->width || fmt.fmt.pix.height != dev->run->height) {
		LOG_ERROR("Requested resolution=%dx%d is unavailable", dev->run->width, dev->run->height);
	}
	dev->run->width = fmt.fmt.pix.width;
	dev->run->height = fmt.fmt.pix.height;
	LOG_INFO("Using resolution: %dx%d", dev->run->width, dev->run->height);

	// Check format
	if (fmt.fmt.pix.pixelformat != dev->format) {
		char format_requested_str[8];
		char format_obtained_str[8];
		char *format_str_nullable;

		LOG_ERROR(
			"Could not obtain the requested pixelformat=%s; driver gave us %s",
			_format_to_string_auto(format_requested_str, 8, dev->format),
			_format_to_string_auto(format_obtained_str, 8, fmt.fmt.pix.pixelformat)
		);

		if ((format_str_nullable = (char *)_format_to_string_null(fmt.fmt.pix.pixelformat)) != NULL) {
			LOG_INFO(
				"Falling back to %s mode (consider using '--format=%s' option)",
				format_str_nullable,
				format_str_nullable
			);
		} else {
			LOG_ERROR("Unsupported pixel format");
			return -1;
		}
	}
	dev->run->format = fmt.fmt.pix.pixelformat;
	return 0;
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
		LOG_ERROR("Insufficient buffer memory: %d", req.count);
		return -1;
	} else {
		LOG_INFO("Requested %d device buffers, got %d", dev->n_buffers, req.count);
	}

	LOG_DEBUG("Allocating device buffers ...");

	A_CALLOC(dev->run->buffers, req.count, sizeof(*dev->run->buffers));
	for (dev->run->n_buffers = 0; dev->run->n_buffers < req.count; ++dev->run->n_buffers) {
		struct v4l2_buffer buf;

		MEMSET_ZERO(buf);
		buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buf.memory = V4L2_MEMORY_MMAP;
		buf.index = dev->run->n_buffers;

		LOG_DEBUG("Calling ioctl(VIDIOC_QUERYBUF) for device buffer %d ...", dev->run->n_buffers);
		if (xioctl(dev->run->fd, VIDIOC_QUERYBUF, &buf) < 0) {
			LOG_PERROR("Can't VIDIOC_QUERYBUF");
			return -1;
		}

		LOG_DEBUG("Mapping device buffer %d ...", dev->run->n_buffers);
		dev->run->buffers[dev->run->n_buffers].length = buf.length;
		dev->run->buffers[dev->run->n_buffers].start = mmap(NULL, buf.length, PROT_READ|PROT_WRITE, MAP_SHARED, dev->run->fd, buf.m.offset);
		if (dev->run->buffers[dev->run->n_buffers].start == MAP_FAILED) {
			LOG_PERROR("Can't map device buffer %d", dev->run->n_buffers);
			return -1;
		}
	}
	return 0;
}

static int _device_open_queue_buffers(struct device_t *dev) {
	for (unsigned index = 0; index < dev->run->n_buffers; ++index) {
		struct v4l2_buffer buf;

		MEMSET_ZERO(buf);
		buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buf.memory = V4L2_MEMORY_MMAP;
		buf.index = index;

		LOG_DEBUG("Calling ioctl(VIDIOC_QBUF) for buffer %d ...", index);
		if (xioctl(dev->run->fd, VIDIOC_QBUF, &buf) < 0) {
			LOG_PERROR("Can't VIDIOC_QBUF");
			return -1;
		}
	}
	return 0;
}

static void _device_open_alloc_picbufs(struct device_t *dev) {
	LOG_DEBUG("Allocating picture buffers ...");

	A_CALLOC(dev->run->pictures, dev->run->n_buffers, sizeof(*dev->run->pictures));

	unsigned picture_size = dev->run->width * dev->run->height << 1;

	for (unsigned index = 0; index < dev->run->n_buffers; ++index) {
		LOG_DEBUG("Allocating picture buffer %d ...", index);
		A_MALLOC(dev->run->pictures[index], picture_size);
	}
}

static const char *_format_to_string_auto(char *buf, const size_t length, const unsigned format) {
	if (length < 8) {
		buf[0] = '\0';
	} else {
		buf[0] = format & 0x7f;
		buf[1] = (format >> 8) & 0x7f;
		buf[2] = (format >> 16) & 0x7f;
		buf[3] = (format >> 24) & 0x7f;
		if (format & (1 << 31)) {
			buf[4] = '-';
			buf[5] = 'B';
			buf[6] = 'E';
			buf[7] = '\0';
		} else {
			buf[4] = '\0';
		}
	}
	return buf;
}

static const char *_format_to_string_null(const unsigned format) {
    for (unsigned index = 0; index < sizeof(FORMATS) / sizeof(FORMATS[0]); ++index) {
		if (format == FORMATS[index].format) {
			return FORMATS[index].name;
		}
    }
    return NULL;
}

static const char *_standard_to_string(v4l2_std_id standard) {
	for (unsigned index = 0; index < sizeof(STANDARDS) / sizeof(STANDARDS[0]); ++index) {
		if (standard == STANDARDS[index].standard) {
			return STANDARDS[index].name;
		}
	}
	return STANDARDS[0].name;
}
