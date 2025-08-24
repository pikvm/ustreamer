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


#include "capture.h"

#include <stdlib.h>
#include <stdatomic.h>
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
#include "errors.h"
#include "tools.h"
#include "array.h"
#include "logging.h"
#include "threading.h"
#include "frame.h"
#include "xioctl.h"
#include "tc358743.h"


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
	{"NV12",	V4L2_PIX_FMT_NV12},
	{"NV16",	V4L2_PIX_FMT_NV16},
	{"NV24",	V4L2_PIX_FMT_NV24},
	{"YUYV",	V4L2_PIX_FMT_YUYV},
	{"YVYU",	V4L2_PIX_FMT_YVYU},
	{"UYVY",	V4L2_PIX_FMT_UYVY},
	{"YUV420",	V4L2_PIX_FMT_YUV420},
	{"YVU420",	V4L2_PIX_FMT_YVU420},
	{"GREY",	V4L2_PIX_FMT_GREY},
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

static int _capture_wait_buffer(us_capture_s *cap);
static int _capture_consume_event(const us_capture_s *cap);
static void _v4l2_buffer_copy(const struct v4l2_buffer *src, struct v4l2_buffer *dest);
static bool _capture_is_buffer_valid(const us_capture_s *cap, const struct v4l2_buffer *buf, const u8 *data);
static int _capture_open_check_cap(us_capture_s *cap);
static int _capture_open_dv_timings(us_capture_s *cap, bool apply);
static int _capture_open_format(us_capture_s *cap, bool first);
static void _capture_open_hw_fps(us_capture_s *cap);
static void _capture_open_jpeg_quality(us_capture_s *cap);
static int _capture_open_io_method(us_capture_s *cap);
static int _capture_open_io_method_mmap(us_capture_s *cap);
static int _capture_open_io_method_userptr(us_capture_s *cap);
static int _capture_open_queue_buffers(us_capture_s *cap);
static int _capture_open_export_to_dma(us_capture_s *cap);
static int _capture_apply_resolution(us_capture_s *cap, uint width, uint height, float hz);

static void _capture_apply_controls(const us_capture_s *cap);
static int _capture_query_control(
	const us_capture_s *cap, struct v4l2_queryctrl *query,
	const char *name, uint cid, bool quiet);
static void _capture_set_control(
	const us_capture_s *cap, const struct v4l2_queryctrl *query,
	const char *name, uint cid, int value, bool quiet);

static const char *_format_to_string_nullable(uint format);
static const char *_format_to_string_supported(uint format);
static const char *_standard_to_string(v4l2_std_id standard);
static const char *_io_method_to_string_supported(enum v4l2_memory io_method);


#define _LOG_ERROR(x_msg, ...)	US_LOG_ERROR("CAP: " x_msg, ##__VA_ARGS__)
#define _LOG_PERROR(x_msg, ...)	US_LOG_PERROR("CAP: " x_msg, ##__VA_ARGS__)
#define _LOG_INFO(x_msg, ...)		US_LOG_INFO("CAP: " x_msg, ##__VA_ARGS__)
#define _LOG_VERBOSE(x_msg, ...)	US_LOG_VERBOSE("CAP: " x_msg, ##__VA_ARGS__)
#define _LOG_DEBUG(x_msg, ...)	US_LOG_DEBUG("CAP: " x_msg, ##__VA_ARGS__)


us_capture_s *us_capture_init(void) {
	us_capture_runtime_s *run;
	US_CALLOC(run, 1);
	run->fd = -1;

	us_capture_s *cap;
	US_CALLOC(cap, 1);
	cap->path = "/dev/video0";
	cap->width = 1920;
	cap->height = 1080;
	cap->format = V4L2_PIX_FMT_YUYV;
	cap->jpeg_quality = 80;
	cap->standard = V4L2_STD_UNKNOWN;
	cap->io_method = V4L2_MEMORY_MMAP;
	cap->n_bufs = us_get_cores_available() + 1;
	cap->min_frame_size = 128;
	cap->timeout = 1;
	cap->run = run;
	return cap;
}

void us_capture_destroy(us_capture_s *cap) {
	free(cap->run);
	free(cap);
}

int us_capture_parse_format(const char *str) {
	US_ARRAY_ITERATE(_FORMATS, 0, item, {
		if (!strcasecmp(item->name, str)) {
			return item->format;
		}
	});
	return -1;
}

int us_capture_parse_standard(const char *str) {
	US_ARRAY_ITERATE(_STANDARDS, 0, item, {
		if (!strcasecmp(item->name, str)) {
			return item->standard;
		}
	});
	return -1;
}

int us_capture_parse_io_method(const char *str) {
	US_ARRAY_ITERATE(_IO_METHODS, 0, item, {
		if (!strcasecmp(item->name, str)) {
			return item->io_method;
		}
	});
	return -1;
}

int us_capture_open(us_capture_s *cap) {
	us_capture_runtime_s *const run = cap->run;

	if (access(cap->path, R_OK | W_OK) < 0) {
		US_ONCE_FOR(run->open_error_once, -errno, {
			US_LOG_PERROR("No access to capture device");
		});
		goto error_no_device;
	}

	_LOG_DEBUG("Opening capture device ...");
	if ((run->fd = open(cap->path, O_RDWR | O_NONBLOCK)) < 0) {
		_LOG_PERROR("Can't open capture device");
		goto error;
	}
	_LOG_DEBUG("Capture device fd=%d opened", run->fd);

	if (cap->dv_timings && cap->persistent) {
		struct v4l2_control ctl = {.id = V4L2_CID_DV_RX_POWER_PRESENT};
		if (!us_xioctl(run->fd, VIDIOC_G_CTRL, &ctl)) {
			if (!ctl.value) {
				goto error_no_cable;
			}
		}
		_LOG_DEBUG("Probing DV-timings or QuerySTD ...");
		switch (_capture_open_dv_timings(cap, false)) {
			case 0: break;
			case US_ERROR_NO_SIGNAL: goto error_no_signal;
			case US_ERROR_NO_SYNC: goto error_no_sync;
			default: goto error;
		}
	}

	US_LOG_INFO("Using V4L2 device: %s", cap->path);

	if (_capture_open_check_cap(cap) < 0) {
		goto error;
	}
	if (_capture_apply_resolution(cap, cap->width, cap->height, cap->run->hz)) {
		goto error;
	}
	if (cap->dv_timings && _capture_open_dv_timings(cap, true) < 0) {
		goto error;
	}
	if (_capture_open_format(cap, true) < 0) {
		goto error;
	}
	if (cap->dv_timings && cap->persistent) {
		struct v4l2_control ctl = {.id = TC358743_CID_LANES_ENOUGH};
		if (!us_xioctl(run->fd, VIDIOC_G_CTRL, &ctl)) {
			if (!ctl.value) {
				_LOG_ERROR("Not enough lanes, hardware can't handle this signal");
				goto error_no_lanes;
			}
		}
	}
	_capture_open_hw_fps(cap);
	_capture_open_jpeg_quality(cap);
	if (_capture_open_io_method(cap) < 0) {
		goto error;
	}
	if (_capture_open_queue_buffers(cap) < 0) {
		goto error;
	}
	if (cap->dma_export && !us_is_jpeg(run->format)) {
		// uStreamer doesn't have any component that could handle JPEG capture via DMA
		run->dma = !_capture_open_export_to_dma(cap);
		if (!run->dma && cap->dma_required) {
			goto error;
		}
	}
	_capture_apply_controls(cap);

	enum v4l2_buf_type type = run->capture_type;
	if (us_xioctl(run->fd, VIDIOC_STREAMON, &type) < 0) {
		_LOG_PERROR("Can't start capturing");
		goto error;
	}
	run->streamon = true;

	run->open_error_once = 0;
	_LOG_INFO("Capturing started");
	return 0;

error_no_device:
	us_capture_close(cap);
	return US_ERROR_NO_DEVICE;

error_no_cable:
	us_capture_close(cap);
	return US_ERROR_NO_CABLE;

error_no_signal:
	US_ONCE_FOR(run->open_error_once, __LINE__, { _LOG_ERROR("No signal from source"); });
	us_capture_close(cap);
	return US_ERROR_NO_SIGNAL;

error_no_sync:
	US_ONCE_FOR(run->open_error_once, __LINE__, { _LOG_ERROR("No sync on signal"); });
	us_capture_close(cap);
	return US_ERROR_NO_SYNC;

error_no_lanes:
	us_capture_close(cap);
	return US_ERROR_NO_LANES;

error:
	run->open_error_once = 0;
	us_capture_close(cap);
	return -1;
}

void us_capture_close(us_capture_s *cap) {
	us_capture_runtime_s *const run = cap->run;

	bool say = false;

	if (run->streamon) {
		say = true;
		_LOG_DEBUG("Calling VIDIOC_STREAMOFF ...");
		enum v4l2_buf_type type = run->capture_type;
		if (us_xioctl(run->fd, VIDIOC_STREAMOFF, &type) < 0) {
			_LOG_PERROR("Can't stop capturing");
		}
		run->streamon = false;
	}

	if (run->bufs != NULL) {
		say = true;
		_LOG_DEBUG("Releasing HW buffers ...");
		for (uint index = 0; index < run->n_bufs; ++index) {
			us_capture_hwbuf_s *hw = &run->bufs[index];

			US_CLOSE_FD(hw->dma_fd);

			if (cap->io_method == V4L2_MEMORY_MMAP) {
				if (hw->raw.allocated > 0 && hw->raw.data != NULL) {
					if (munmap(hw->raw.data, hw->raw.allocated) < 0) {
						_LOG_PERROR("Can't unmap HW buffer=%u", index);
					}
				}
			} else { // V4L2_MEMORY_USERPTR
				US_DELETE(hw->raw.data, free);
			}

			if (run->capture_mplane) {
				free(hw->buf.m.planes);
			}
		}
		US_DELETE(run->bufs, free);
		run->n_bufs = 0;
	}

	US_CLOSE_FD(run->fd);

	if (say) {
		_LOG_INFO("Capturing stopped");
	}
}

int us_capture_hwbuf_grab(us_capture_s *cap, us_capture_hwbuf_s **hw) {
	// Это сложная функция, которая делает сразу много всего, чтобы получить новый фрейм.
	//   - Вызывается _capture_wait_buffer() с select() внутри, чтобы подождать новый фрейм
	//     или эвент V4L2. Обработка эвентов более приоритетна, чем кадров.
	//   - Если есть новые фреймы, то пропустить их все, пока не закончатся и вернуть
	//     самый-самый свежий, содержащий при этом валидные данные.
	//   - Если таковых не нашлось, вернуть US_ERROR_NO_DATA.
	//   - Ошибка -1 возвращается при любых сбоях.

	if (_capture_wait_buffer(cap) < 0) {
		return -1;
	}

	us_capture_runtime_s *const run = cap->run;

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

	_LOG_DEBUG("Grabbing hw buffer ...");

	do {
		struct v4l2_buffer new = {0};
		struct v4l2_plane new_planes[VIDEO_MAX_PLANES] = {0};
		new.type = run->capture_type;
		new.memory = cap->io_method;
		if (run->capture_mplane) {
			new.length = VIDEO_MAX_PLANES;
			new.m.planes = new_planes;
		}
		
		const bool new_got = (us_xioctl(run->fd, VIDIOC_DQBUF, &new) >= 0);

		if (new_got) {
			if (new.index >= run->n_bufs) {
				_LOG_ERROR("V4L2 error: grabbed invalid HW buffer=%u, n_bufs=%u", new.index, run->n_bufs);
				return -1;
			}

#			define GRABBED(x_buf) run->bufs[x_buf.index].grabbed
#			define FRAME_DATA(x_buf) run->bufs[x_buf.index].raw.data

			if (GRABBED(new)) {
				_LOG_ERROR("V4L2 error: grabbed HW buffer=%u is already used", new.index);
				return -1;
			}
			GRABBED(new) = true;

			if (run->capture_mplane) {
				new.bytesused = new.m.planes[0].bytesused;
			}

			broken = !_capture_is_buffer_valid(cap, &new, FRAME_DATA(new));
			if (broken) {
				_LOG_DEBUG("Releasing HW buffer=%u (broken frame) ...", new.index);
				if (us_xioctl(run->fd, VIDIOC_QBUF, &new) < 0) {
					_LOG_PERROR("Can't release HW buffer=%u (broken frame)", new.index);
					return -1;
				}
				GRABBED(new) = false;
				continue;
			}

			if (buf_got) {
				if (us_xioctl(run->fd, VIDIOC_QBUF, &buf) < 0) {
					_LOG_PERROR("Can't release HW buffer=%u (skipped frame)", buf.index);
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
					return US_ERROR_NO_DATA; // If we have only broken frames on this capture session
				}
			}
			_LOG_PERROR("Can't grab HW buffer");
			return -1;
		}
	} while (true);

	*hw = &run->bufs[buf.index];
	atomic_store(&(*hw)->refs, 0);
	(*hw)->raw.dma_fd = (*hw)->dma_fd;
	(*hw)->raw.used = buf.bytesused;
	(*hw)->raw.width = run->width;
	(*hw)->raw.height = run->height;
	(*hw)->raw.format = run->format;
	(*hw)->raw.stride = run->stride;
	(*hw)->raw.online = true;
	_v4l2_buffer_copy(&buf, &(*hw)->buf);
	(*hw)->raw.grab_ts = (ldf)((buf.timestamp.tv_sec * (u64)1000) + (buf.timestamp.tv_usec / 1000)) / 1000;

	_LOG_DEBUG("Grabbed HW buffer=%u: bytesused=%u, grab_ts=%.3Lf, latency=%.3Lf, skipped=%u",
		buf.index, buf.bytesused, (*hw)->raw.grab_ts, us_get_now_monotonic() - (*hw)->raw.grab_ts, skipped);
	return buf.index;
}

int us_capture_hwbuf_release(const us_capture_s *cap, us_capture_hwbuf_s *hw) {
	assert(atomic_load(&hw->refs) == 0);
	const uint index = hw->buf.index;
	_LOG_DEBUG("Releasing HW buffer=%u ...", index);
	if (us_xioctl(cap->run->fd, VIDIOC_QBUF, &hw->buf) < 0) {
		_LOG_PERROR("Can't release HW buffer=%u", index);
		return -1;
	}
	hw->grabbed = false;
	_LOG_DEBUG("HW buffer=%u released", index);
	return 0;
}

void us_capture_hwbuf_incref(us_capture_hwbuf_s *hw) {
	atomic_fetch_add(&hw->refs, 1);
}

void us_capture_hwbuf_decref(us_capture_hwbuf_s *hw) {
	atomic_fetch_sub(&hw->refs, 1);
}

int _capture_wait_buffer(us_capture_s *cap) {
	us_capture_runtime_s *const run = cap->run;

#	define INIT_FD_SET(x_set) \
		fd_set x_set; FD_ZERO(&x_set); FD_SET(run->fd, &x_set);
	INIT_FD_SET(read_fds);
	INIT_FD_SET(error_fds);
#	undef INIT_FD_SET

	// Раньше мы проверяли и has_write, но потом выяснилось, что libcamerify зачем-то
	// генерирует эвенты на запись, вероятно ошибочно. Судя по всему, игнорирование
	// has_write не делает никому плохо.

	struct timeval timeout;
	timeout.tv_sec = cap->timeout;
	timeout.tv_usec = 0;

	_LOG_DEBUG("Calling select() on video device ...");

	bool has_read = false;
	bool has_error = false;
	const int selected = select(run->fd + 1, &read_fds, NULL, &error_fds, &timeout);
	if (selected > 0) {
		has_read = FD_ISSET(run->fd, &read_fds);
		has_error = FD_ISSET(run->fd, &error_fds);
	}
	_LOG_DEBUG("Device select() --> %d; has_read=%d, has_error=%d", selected, has_read, has_error);

	if (selected < 0) {
		if (errno != EINTR) {
			_LOG_PERROR("Device select() error");
		}
		return -1;
	} else if (selected == 0) {
		_LOG_ERROR("Device select() timeout");
		return -1;
	} else {
		if (has_error && _capture_consume_event(cap) < 0) {
			return -1; // Restart required
		}
	}
	return 0;
}

static int _capture_consume_event(const us_capture_s *cap) {
	struct v4l2_event event;
	if (us_xioctl(cap->run->fd, VIDIOC_DQEVENT, &event) < 0) {
		_LOG_PERROR("Can't consume V4L2 event");
		return -1;
	}
	switch (event.type) {
		case V4L2_EVENT_SOURCE_CHANGE:
			_LOG_INFO("Got V4L2_EVENT_SOURCE_CHANGE: Source changed");
			return -1;
		case V4L2_EVENT_EOS:
			_LOG_INFO("Got V4L2_EVENT_EOS: End of stream");
			return -1;
	}
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

bool _capture_is_buffer_valid(const us_capture_s *cap, const struct v4l2_buffer *buf, const u8 *data) {
	// Workaround for broken, corrupted frames:
	// Under low light conditions corrupted frames may get captured.
	// The good thing is such frames are quite small compared to the regular frames.
	// For example a VGA (640x480) webcam frame is normally >= 8kByte large,
	// corrupted frames are smaller.
	if (buf->bytesused < cap->min_frame_size) {
		_LOG_DEBUG("Dropped too small frame, assuming it was broken: buffer=%u, bytesused=%u",
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
	if (us_is_jpeg(cap->run->format)) {
		if (buf->bytesused < 125) {
			// https://stackoverflow.com/questions/2253404/what-is-the-smallest-valid-jpeg-file-size-in-bytes
			_LOG_DEBUG("Discarding invalid frame, too small to be a valid JPEG: bytesused=%u",
				buf->bytesused);
			return false;
		}

		const u16 begin_marker = (((u16)(data[0]) << 8) | data[1]);
		if (begin_marker != 0xFFD8) {
			_LOG_DEBUG("Discarding JPEG frame with invalid header: begin_marker=0x%04x, bytesused=%u",
				begin_marker, buf->bytesused);
			return false;
		}

		const u8 *const end_ptr = data + buf->bytesused - 2;
		const u16 end_marker = (((u16)(end_ptr[0]) << 8) | end_ptr[1]);
		if (end_marker != 0xFFD9 && end_marker != 0xD900 && end_marker != 0x0000) {
			if (!cap->allow_truncated_frames) {
				_LOG_DEBUG("Discarding truncated JPEG frame: end_marker=0x%04x, bytesused=%u",
					end_marker, buf->bytesused);
				return false;
			}
			_LOG_DEBUG("Got truncated JPEG frame: end_marker=0x%04x, bytesused=%u",
				end_marker, buf->bytesused);
		}
	}

	return true;
}

static int _capture_open_check_cap(us_capture_s *cap) {
	us_capture_runtime_s *const run = cap->run;

	struct v4l2_capability cpb = {0};
	_LOG_DEBUG("Querying device capabilities ...");
	if (us_xioctl(run->fd, VIDIOC_QUERYCAP, &cpb) < 0) {
		_LOG_PERROR("Can't query device capabilities");
		return -1;
	}

	if (cpb.capabilities & V4L2_CAP_VIDEO_CAPTURE) {
		run->capture_type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		run->capture_mplane = false;
		_LOG_INFO("Using capture type: single-planar");
	} else if (cpb.capabilities & V4L2_CAP_VIDEO_CAPTURE_MPLANE) {
		run->capture_type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
		run->capture_mplane = true;
		_LOG_INFO("Using capture type: multi-planar");
	} else {
		_LOG_ERROR("Video capture is not supported by device");
		return -1;
	}

	if (!(cpb.capabilities & V4L2_CAP_STREAMING)) {
		_LOG_ERROR("Device doesn't support streaming IO");
		return -1;
	}

	if (!run->capture_mplane) {
		int input = cap->input; // Needs a pointer to int for ioctl()
		_LOG_INFO("Using input channel: %d", input);
		if (us_xioctl(run->fd, VIDIOC_S_INPUT, &input) < 0) {
			_LOG_ERROR("Can't set input channel");
			return -1;
		}
	}

	if (cap->standard != V4L2_STD_UNKNOWN) {
		_LOG_INFO("Using TV standard: %s", _standard_to_string(cap->standard));
		if (us_xioctl(run->fd, VIDIOC_S_STD, &cap->standard) < 0) {
			_LOG_ERROR("Can't set video standard");
			return -1;
		}
	} else {
		_LOG_DEBUG("Using TV standard: DEFAULT");
	}
	return 0;
}

static int _capture_open_dv_timings(us_capture_s *cap, bool apply) {
	// Just probe only if @apply is false

	const us_capture_runtime_s *const run = cap->run;

	int dv_errno = 0;

	struct v4l2_dv_timings dv = {0};
	_LOG_DEBUG("Querying DV-timings (apply=%u) ...", apply);
	if (us_xioctl(run->fd, VIDIOC_QUERY_DV_TIMINGS, &dv) < 0) {
		// TC358743 errors here (see in the kernel: drivers/media/i2c/tc358743.c):
		//   - ENOLINK: No valid signal (SYS_STATUS & MASK_S_TMDS)
		//   - ENOLCK:  No sync on signal (SYS_STATUS & MASK_S_SYNC)
		switch (errno) {
			case ENOLINK: return US_ERROR_NO_SIGNAL;
			case ENOLCK: return US_ERROR_NO_SYNC;
		}
		dv_errno = errno;
		goto querystd;
	} else if (!apply) {
		goto probe_only;
	}

	float hz = 0;
	if (dv.type == V4L2_DV_BT_656_1120) {
		// See v4l2_print_dv_timings() in the kernel
		const uint htot = V4L2_DV_BT_FRAME_WIDTH(&dv.bt);
		const uint vtot = V4L2_DV_BT_FRAME_HEIGHT(&dv.bt) / (dv.bt.interlaced ? 2 : 1);
		const uint fps = ((htot * vtot) > 0 ? ((100 * (u64)dv.bt.pixelclock)) / (htot * vtot) : 0);
		hz = (fps / 100) + (fps % 100) / 100.0;
		_LOG_INFO("Detected DV-timings: %ux%u%s%.02f, pixclk=%llu, vsync=%u, hsync=%u",
			dv.bt.width, dv.bt.height, (dv.bt.interlaced ? "i" : "p"), hz,
			(ull)dv.bt.pixelclock, dv.bt.vsync, dv.bt.hsync); // See #11 about %llu
	} else {
		_LOG_INFO("Detected DV-timings: %ux%u, pixclk=%llu, vsync=%u, hsync=%u",
			dv.bt.width, dv.bt.height,
			(ull)dv.bt.pixelclock, dv.bt.vsync, dv.bt.hsync);
	}

	_LOG_DEBUG("Applying DV-timings ...");
	if (us_xioctl(run->fd, VIDIOC_S_DV_TIMINGS, &dv) < 0) {
		_LOG_PERROR("Failed to apply DV-timings");
		return -1;
	}
	if (_capture_apply_resolution(cap, dv.bt.width, dv.bt.height, hz) < 0) {
		return -1;
	}
	goto subscribe;

querystd:
	_LOG_DEBUG("Failed to query DV-timings, trying QuerySTD ...");
	if (us_xioctl(run->fd, VIDIOC_QUERYSTD, &cap->standard) < 0) {
		if (apply) {
			char *std_error = us_errno_to_string(errno); // Read the errno first
			char *dv_error = us_errno_to_string(dv_errno);
			_LOG_ERROR("Failed to query DV-timings (%s) and QuerySTD (%s)", dv_error, std_error);
			free(dv_error);
			free(std_error);
		}
		return -1;
	} else if (!apply) {
		goto probe_only;
	}
	if (us_xioctl(run->fd, VIDIOC_S_STD, &cap->standard) < 0) {
		_LOG_PERROR("Can't set apply standard: %s", _standard_to_string(cap->standard));
		return -1;
	}
	_LOG_DEBUG("Applied new video standard: %s", _standard_to_string(cap->standard));

subscribe:
	; // Empty statement for the goto label above
	struct v4l2_event_subscription sub = {.type = V4L2_EVENT_SOURCE_CHANGE};
	_LOG_DEBUG("Subscribing to V4L2_EVENT_SOURCE_CHANGE ...")
	if (us_xioctl(cap->run->fd, VIDIOC_SUBSCRIBE_EVENT, &sub) < 0) {
		_LOG_PERROR("Can't subscribe to V4L2_EVENT_SOURCE_CHANGE");
		return -1;
	}

probe_only:
	return 0;
}

static int _capture_open_format(us_capture_s *cap, bool first) {
	us_capture_runtime_s *const run = cap->run;

	const uint stride = us_align_size(run->width, 32) << 1;

	struct v4l2_format fmt = {0};
	fmt.type = run->capture_type;
	if (run->capture_mplane) {
		fmt.fmt.pix_mp.width = run->width;
		fmt.fmt.pix_mp.height = run->height;
		fmt.fmt.pix_mp.pixelformat = cap->format;
		fmt.fmt.pix_mp.field = V4L2_FIELD_ANY;
		fmt.fmt.pix_mp.flags = 0;
		fmt.fmt.pix_mp.num_planes = 1;
	} else {
		fmt.fmt.pix.width = run->width;
		fmt.fmt.pix.height = run->height;
		fmt.fmt.pix.pixelformat = cap->format;
		fmt.fmt.pix.field = V4L2_FIELD_ANY;
		fmt.fmt.pix.bytesperline = stride;
	}

	// Set format
	_LOG_DEBUG("Probing device format=%s, stride=%u, resolution=%ux%u ...",
		_format_to_string_supported(cap->format), stride, run->width, run->height);
	if (us_xioctl(run->fd, VIDIOC_S_FMT, &fmt) < 0) {
		_LOG_PERROR("Can't set device format");
		return -1;
	}

	if (fmt.type != run->capture_type) {
		_LOG_ERROR("Capture format mismatch, please report to the developer");
		return -1;
	}

#	define FMT(x_next)	(run->capture_mplane ? fmt.fmt.pix_mp.x_next : fmt.fmt.pix.x_next)
#	define FMTS(x_next)	(run->capture_mplane ? fmt.fmt.pix_mp.plane_fmt[0].x_next : fmt.fmt.pix.x_next)

	// Check resolution
	bool retry = false;
	if (FMT(width) != run->width || FMT(height) != run->height) {
		_LOG_ERROR("Requested resolution=%ux%u is unavailable", run->width, run->height);
		retry = true;
	}
	if (_capture_apply_resolution(cap, FMT(width), FMT(height), run->hz) < 0) {
		return -1;
	}
	if (first && retry) {
		return _capture_open_format(cap, false);
	}
	_LOG_INFO("Using resolution: %ux%u", run->width, run->height);

	// Check format
	if (FMT(pixelformat) != cap->format) {
		_LOG_ERROR("Could not obtain the requested format=%s; driver gave us %s",
			_format_to_string_supported(cap->format),
			_format_to_string_supported(FMT(pixelformat)));

		const char *format_str;
		if ((format_str = (char*)_format_to_string_nullable(FMT(pixelformat))) != NULL) {
			_LOG_INFO("Falling back to format=%s", format_str);
		} else {
			char fourcc_str[8];
			_LOG_ERROR("Unsupported format=%s (fourcc)",
				us_fourcc_to_string(FMT(pixelformat), fourcc_str, 8));
			return -1;
		}
	}

	run->format = FMT(pixelformat);
	_LOG_INFO("Using format: %s", _format_to_string_supported(run->format));

	if (cap->format_swap_rgb) {
		// Userspace workaround for TC358743 RGB/BGR bug:
		//   - https://github.com/raspberrypi/linux/issues/6068
		uint swapped = 0;
		switch (run->format) {
			case V4L2_PIX_FMT_RGB24: swapped = V4L2_PIX_FMT_BGR24; break;
			case V4L2_PIX_FMT_BGR24: swapped = V4L2_PIX_FMT_RGB24; break;
		}
		if (swapped > 0) {
			_LOG_INFO("Using format swap: %s -> %s",
				_format_to_string_supported(run->format),
				_format_to_string_supported(swapped));
			run->format = swapped;
		}
	}

	run->stride = FMTS(bytesperline);
	run->raw_size = FMTS(sizeimage); // Only for userptr

#	undef FMTS
#	undef FMT

	return 0;
}

static void _capture_open_hw_fps(us_capture_s *cap) {
	us_capture_runtime_s *const run = cap->run;

	run->hw_fps = 0;

	struct v4l2_streamparm setfps = {.type = run->capture_type};
	_LOG_DEBUG("Querying HW FPS ...");
	if (us_xioctl(run->fd, VIDIOC_G_PARM, &setfps) < 0) {
		if (errno == ENOTTY) { // Quiet message for TC358743
			_LOG_INFO("Querying HW FPS changing is not supported");
		} else {
			_LOG_PERROR("Can't query HW FPS changing");
		}
		return;
	}

	if (!(setfps.parm.capture.capability & V4L2_CAP_TIMEPERFRAME)) {
		_LOG_INFO("Changing HW FPS is not supported");
		return;
	}

#	define SETFPS_TPF(x_next) setfps.parm.capture.timeperframe.x_next

	US_MEMSET_ZERO(setfps);
	setfps.type = run->capture_type;
	SETFPS_TPF(numerator) = 1;
	SETFPS_TPF(denominator) = (cap->desired_fps == 0 ? 255 : cap->desired_fps);

	if (us_xioctl(run->fd, VIDIOC_S_PARM, &setfps) < 0) {
		_LOG_PERROR("Can't set HW FPS");
		return;
	}

	if (SETFPS_TPF(numerator) != 1) {
		_LOG_ERROR("Invalid HW FPS numerator: %u != 1", SETFPS_TPF(numerator));
		return;
	}

	if (SETFPS_TPF(denominator) == 0) { // Не знаю, бывает ли так, но пускай на всякий случай
		_LOG_ERROR("Invalid HW FPS denominator: 0");
		return;
	}

	run->hw_fps = SETFPS_TPF(denominator);
	if (cap->desired_fps != run->hw_fps) {
		_LOG_INFO("Using HW FPS: %u -> %u (coerced)", cap->desired_fps, run->hw_fps);
	} else {
		_LOG_INFO("Using HW FPS: %u", run->hw_fps);
	}

#	undef SETFPS_TPF
}

static void _capture_open_jpeg_quality(us_capture_s *cap) {
	us_capture_runtime_s *const run = cap->run;
	uint quality = 0;
	if (us_is_jpeg(run->format)) {
		struct v4l2_jpegcompression comp = {0};
		if (us_xioctl(run->fd, VIDIOC_G_JPEGCOMP, &comp) < 0) {
			_LOG_ERROR("Device doesn't support setting of HW encoding quality parameters");
		} else {
			comp.quality = cap->jpeg_quality;
			if (us_xioctl(run->fd, VIDIOC_S_JPEGCOMP, &comp) < 0) {
				_LOG_ERROR("Can't change MJPEG quality for JPEG source with HW pass-through encoder");
			} else {
				quality = cap->jpeg_quality;
			}
		}
	}
	run->jpeg_quality = quality;
}

static int _capture_open_io_method(us_capture_s *cap) {
	_LOG_INFO("Using IO method: %s", _io_method_to_string_supported(cap->io_method));
	switch (cap->io_method) {
		case V4L2_MEMORY_MMAP: return _capture_open_io_method_mmap(cap);
		case V4L2_MEMORY_USERPTR: return _capture_open_io_method_userptr(cap);
		default: assert(0 && "Unsupported IO method");
	}
	return -1;
}

static int _capture_open_io_method_mmap(us_capture_s *cap) {
	us_capture_runtime_s *const run = cap->run;

	struct v4l2_requestbuffers req = {
		.count = cap->n_bufs,
		.type = run->capture_type,
		.memory = V4L2_MEMORY_MMAP,
	};
	_LOG_DEBUG("Requesting %u device buffers for MMAP ...", req.count);
	if (us_xioctl(run->fd, VIDIOC_REQBUFS, &req) < 0) {
		_LOG_PERROR("Device '%s' doesn't support MMAP method", cap->path);
		return -1;
	}

	if (req.count < 1) {
		_LOG_ERROR("Insufficient buffer memory: %u", req.count);
		return -1;
	} else {
		_LOG_INFO("Requested %u device buffers, got %u", cap->n_bufs, req.count);
	}

	_LOG_DEBUG("Allocating device buffers ...");

	US_CALLOC(run->bufs, req.count);

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

		_LOG_DEBUG("Calling us_xioctl(VIDIOC_QUERYBUF) for device buffer=%u ...", run->n_bufs);
		if (us_xioctl(run->fd, VIDIOC_QUERYBUF, &buf) < 0) {
			_LOG_PERROR("Can't VIDIOC_QUERYBUF");
			return -1;
		}

		us_capture_hwbuf_s *hw = &run->bufs[run->n_bufs];
		atomic_init(&hw->refs, 0);
		const uz buf_size = (run->capture_mplane ? buf.m.planes[0].length : buf.length);
		const off_t buf_offset = (run->capture_mplane ? buf.m.planes[0].m.mem_offset : buf.m.offset);

		_LOG_DEBUG("Mapping device buffer=%u ...", run->n_bufs);
		if ((hw->raw.data = mmap(
			NULL, buf_size,
			PROT_READ | PROT_WRITE, MAP_SHARED,
			run->fd, buf_offset
		)) == MAP_FAILED) {
			_LOG_PERROR("Can't map device buffer=%u", run->n_bufs);
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

static int _capture_open_io_method_userptr(us_capture_s *cap) {
	us_capture_runtime_s *const run = cap->run;

	struct v4l2_requestbuffers req = {
		.count = cap->n_bufs,
		.type = run->capture_type,
		.memory = V4L2_MEMORY_USERPTR,
	};
	_LOG_DEBUG("Requesting %u device buffers for USERPTR ...", req.count);
	if (us_xioctl(run->fd, VIDIOC_REQBUFS, &req) < 0) {
		_LOG_PERROR("Device '%s' doesn't support USERPTR method", cap->path);
		return -1;
	}

	if (req.count < 1) {
		_LOG_ERROR("Insufficient buffer memory: %u", req.count);
		return -1;
	} else {
		_LOG_INFO("Requested %u device buffers, got %u", cap->n_bufs, req.count);
	}

	_LOG_DEBUG("Allocating device buffers ...");

	US_CALLOC(run->bufs, req.count);

	const uint page_size = getpagesize();
	const uint buf_size = us_align_size(run->raw_size, page_size);

	for (run->n_bufs = 0; run->n_bufs < req.count; ++run->n_bufs) {
		us_capture_hwbuf_s *hw = &run->bufs[run->n_bufs];
		assert((hw->raw.data = aligned_alloc(page_size, buf_size)) != NULL);
		memset(hw->raw.data, 0, buf_size);
		hw->raw.allocated = buf_size;
		if (run->capture_mplane) {
			US_CALLOC(hw->buf.m.planes, VIDEO_MAX_PLANES);
		}
	}
	return 0;
}

static int _capture_open_queue_buffers(us_capture_s *cap) {
	us_capture_runtime_s *const run = cap->run;

	for (uint index = 0; index < run->n_bufs; ++index) {
		struct v4l2_buffer buf = {0};
		struct v4l2_plane planes[VIDEO_MAX_PLANES] = {0};
		buf.type = run->capture_type;
		buf.memory = cap->io_method;
		buf.index = index;
		if (run->capture_mplane) {
			buf.m.planes = planes;
			buf.length = 1;
		}
		
		if (cap->io_method == V4L2_MEMORY_USERPTR) {
			// I am not sure, may be this is incorrect for mplane device, 
			// but i don't have one which supports V4L2_MEMORY_USERPTR
			buf.m.userptr = (unsigned long)run->bufs[index].raw.data;
			buf.length = run->bufs[index].raw.allocated;
		}

		_LOG_DEBUG("Calling us_xioctl(VIDIOC_QBUF) for buffer=%u ...", index);
		if (us_xioctl(run->fd, VIDIOC_QBUF, &buf) < 0) {
			_LOG_PERROR("Can't VIDIOC_QBUF");
			return -1;
		}
	}
	return 0;
}

static int _capture_open_export_to_dma(us_capture_s *cap) {
	us_capture_runtime_s *const run = cap->run;

	for (uint index = 0; index < run->n_bufs; ++index) {
		struct v4l2_exportbuffer exp = {
			.type = run->capture_type,
			.index = index,
		};
		_LOG_DEBUG("Exporting device buffer=%u to DMA ...", index);
		if (us_xioctl(run->fd, VIDIOC_EXPBUF, &exp) < 0) {
			_LOG_PERROR("Can't export device buffer=%u to DMA", index);
			goto error;
		}
		run->bufs[index].dma_fd = exp.fd;
	}
	return 0;

error:
	for (uint index = 0; index < run->n_bufs; ++index) {
		US_CLOSE_FD(run->bufs[index].dma_fd);
	}
	return -1;
}

static int _capture_apply_resolution(us_capture_s *cap, uint width, uint height, float hz) {
	// Тут VIDEO_MIN_* не используются из-за странностей минимального разрешения при отсутствии сигнала
	// у некоторых устройств, например TC358743
	if (
		width == 0 || width > US_VIDEO_MAX_WIDTH
		|| height == 0 || height > US_VIDEO_MAX_HEIGHT
	) {
		_LOG_ERROR("Requested forbidden resolution=%ux%u: min=1x1, max=%ux%u",
			width, height, US_VIDEO_MAX_WIDTH, US_VIDEO_MAX_HEIGHT);
		return -1;
	}
	cap->run->width = width;
	cap->run->height = height;
	cap->run->hz = hz;
	return 0;
}

static void _capture_apply_controls(const us_capture_s *cap) {
#	define SET_CID_VALUE(x_cid, x_field, x_value, x_quiet) { \
			struct v4l2_queryctrl m_query; \
			if (_capture_query_control(cap, &m_query, #x_field, x_cid, x_quiet) == 0) { \
				_capture_set_control(cap, &m_query, #x_field, x_cid, x_value, x_quiet); \
			} \
		}

#	define SET_CID_DEFAULT(x_cid, x_field, x_quiet) { \
			struct v4l2_queryctrl m_query; \
			if (_capture_query_control(cap, &m_query, #x_field, x_cid, x_quiet) == 0) { \
				_capture_set_control(cap, &m_query, #x_field, x_cid, m_query.default_value, x_quiet); \
			} \
		}

#	define CONTROL_MANUAL_CID(x_cid, x_field) { \
			if (cap->ctl.x_field.mode == CTL_MODE_VALUE) { \
				SET_CID_VALUE(x_cid, x_field, cap->ctl.x_field.value, false); \
			} else if (cap->ctl.x_field.mode == CTL_MODE_DEFAULT) { \
				SET_CID_DEFAULT(x_cid, x_field, false); \
			} \
		}

#	define CONTROL_AUTO_CID(x_cid_auto, x_cid_manual, x_field) { \
			if (cap->ctl.x_field.mode == CTL_MODE_VALUE) { \
				SET_CID_VALUE(x_cid_auto, x_field##_auto, 0, true); \
				SET_CID_VALUE(x_cid_manual, x_field, cap->ctl.x_field.value, false); \
			} else if (cap->ctl.x_field.mode == CTL_MODE_AUTO) { \
				SET_CID_VALUE(x_cid_auto, x_field##_auto, 1, false); \
			} else if (cap->ctl.x_field.mode == CTL_MODE_DEFAULT) { \
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

static int _capture_query_control(
	const us_capture_s *cap, struct v4l2_queryctrl *query,
	const char *name, uint cid, bool quiet) {

	// cppcheck-suppress redundantPointerOp
	US_MEMSET_ZERO(*query);
	query->id = cid;

	if (us_xioctl(cap->run->fd, VIDIOC_QUERYCTRL, query) < 0 || query->flags & V4L2_CTRL_FLAG_DISABLED) {
		if (!quiet) {
			_LOG_ERROR("Changing control %s is unsupported", name);
		}
		return -1;
	}
	return 0;
}

static void _capture_set_control(
	const us_capture_s *cap, const struct v4l2_queryctrl *query,
	const char *name, uint cid, int value, bool quiet) {

	if (value < query->minimum || value > query->maximum || value % query->step != 0) {
		if (!quiet) {
			_LOG_ERROR("Invalid value %d of control %s: min=%d, max=%d, default=%d, step=%u",
				value, name, query->minimum, query->maximum, query->default_value, query->step);
		}
		return;
	}

	struct v4l2_control ctl = {
		.id = cid,
		.value = value,
	};
	if (us_xioctl(cap->run->fd, VIDIOC_S_CTRL, &ctl) < 0) {
		if (!quiet) {
			_LOG_PERROR("Can't set control %s", name);
		}
	} else if (!quiet) {
		_LOG_INFO("Applying control %s: %d", name, ctl.value);
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
	return "???";
}

static const char *_io_method_to_string_supported(enum v4l2_memory io_method) {
	US_ARRAY_ITERATE(_IO_METHODS, 0, item, {
		if (item->io_method == io_method) {
			return item->name;
		}
	});
	return "unsupported";
}
