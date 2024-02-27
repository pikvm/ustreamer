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


#include "drm.h"

#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>

#include <xf86drm.h>
#include <xf86drmMode.h>
#include <drm_fourcc.h>
#include <libdrm/drm.h>

#include "../libs/types.h"
#include "../libs/tools.h"
#include "../libs/logging.h"
#include "../libs/frame.h"

#include "ftext.h"


static void _drm_vsync_callback(int fd, uint n_frame, uint sec, uint usec, void *v_run);
static int _drm_expose_raw(us_drm_s *drm, const us_frame_s *frame);
static void _drm_cleanup(us_drm_s *drm);
static int _drm_ensure(us_drm_s *drm, const us_frame_s *frame, float hz);
static int _drm_check_status(us_drm_s *drm);
static int _drm_find_sink(us_drm_s *drm, uint width, uint height, float hz);
static int _drm_init_buffers(us_drm_s *drm);
static int _drm_start_video(us_drm_s *drm);

static u32 _find_crtc(int fd, drmModeRes *res, drmModeConnector *conn, u32 *taken_crtcs);
static const char *_connector_type_to_string(u32 type);
static float _get_refresh_rate(const drmModeModeInfo *mode);


#define _D_LOG_ERROR(x_msg, ...)	US_LOG_ERROR("DRM: " x_msg, ##__VA_ARGS__)
#define _D_LOG_PERROR(x_msg, ...)	US_LOG_PERROR("DRM: " x_msg, ##__VA_ARGS__)
#define _D_LOG_INFO(x_msg, ...)		US_LOG_INFO("DRM: " x_msg, ##__VA_ARGS__)
#define _D_LOG_VERBOSE(x_msg, ...)	US_LOG_VERBOSE("DRM: " x_msg, ##__VA_ARGS__)
#define _D_LOG_DEBUG(x_msg, ...)	US_LOG_DEBUG("DRM: " x_msg, ##__VA_ARGS__)


us_drm_s *us_drm_init(void) {
	us_drm_runtime_s *run;
	US_CALLOC(run, 1);
	run->fd = -1;
	run->status_fd = -1;
	run->ft = us_ftext_init();
	run->state = US_DRM_STATE_CLOSED;

	us_drm_s *drm;
	US_CALLOC(drm, 1);
	drm->path = "/dev/dri/card0";
	drm->port = "HDMI-A-1";
	drm->n_bufs = 4;
	drm->timeout = 5;
	drm->run = run;
	return drm;
}

void us_drm_destroy(us_drm_s *drm) {
	_drm_cleanup(drm);
	us_ftext_destroy(drm->run->ft);
	US_DELETE(drm->run, free);
	US_DELETE(drm, free); // cppcheck-suppress uselessAssignmentPtrArg
}

int us_drm_wait_for_vsync(us_drm_s *drm) {
	us_drm_runtime_s *const run = drm->run;

	if (_drm_ensure(drm, NULL, 0) < 0) {
		return -1;
	}
	if (run->has_vsync) {
		return 0;
	}

	struct timeval timeout = {.tv_sec = drm->timeout};
	fd_set fds;
	FD_ZERO(&fds);
	FD_SET(run->fd, &fds);

	_D_LOG_DEBUG("Calling select() for VSync ...");
	const int result = select(run->fd + 1, &fds, NULL, NULL, &timeout);
	if (result < 0) {
		_D_LOG_PERROR("Can't select(%d) device for VSync", run->fd);
		goto error;
	} else if (result == 0) {
		_D_LOG_ERROR("Device timeout while waiting VSync");
		goto error;
	}

	drmEventContext ctx = {
		.version = DRM_EVENT_CONTEXT_VERSION,
		.page_flip_handler = _drm_vsync_callback,
	};
	_D_LOG_DEBUG("Handling DRM event (maybe VSync) ...");
	if (drmHandleEvent(run->fd, &ctx) < 0) {
		_D_LOG_PERROR("Can't handle DRM event");
		goto error;
	}
	return 0;

error:
	_drm_cleanup(drm);
	_D_LOG_ERROR("Device destroyed due an error (vsync)");
	return -1;
}

int us_drm_expose(us_drm_s *drm, us_drm_expose_e ex, const us_frame_s *frame, float hz) {
	us_drm_runtime_s *const run = drm->run;

	if (_drm_ensure(drm, frame, hz) < 0) {
		return -1;
	}

	const drmModeModeInfo *const mode = &run->mode;
	bool msg_drawn = false;

#	define DRAW_MSG(x_msg) { \
			us_ftext_draw(run->ft, (x_msg), mode->hdisplay, mode->vdisplay); \
			frame = run->ft->frame; \
			msg_drawn = true; \
		}

	if (frame == NULL) {
		switch (ex) {
			case US_DRM_EXPOSE_NO_SIGNAL:
				DRAW_MSG("=== PiKVM ===\n \n< NO SIGNAL >");
				break;
			case US_DRM_EXPOSE_BUSY:
				DRAW_MSG("=== PiKVM ===\n \n< ONLINE IS ACTIVE >");
				break;
			default:
				DRAW_MSG("=== PiKVM ===\n \n< ??? >");
		}
	} else if (mode->hdisplay != frame->width/* || mode->vdisplay != frame->height*/) {
		// XXX: At least we'll try to show something instead of nothing ^^^
		char msg[1024];
		US_SNPRINTF(msg, 1023,
			"=== PiKVM ==="
			"\n \n< UNSUPPORTED RESOLUTION >"
			"\n \n< %ux%up%.02f >"
			"\n \nby this display",
			frame->width, frame->height, hz);
		DRAW_MSG(msg);
	} else if (frame->format != V4L2_PIX_FMT_RGB24) {
		DRAW_MSG(
			"=== PiKVM ==="
			"\n \n< UNSUPPORTED CAPTURE FORMAT >"
			"\n \nIt shouldn't happen ever."
			"\n \nPlease check the logs and report a bug:"
			"\n \n- https://github.com/pikvm/pikvm -");
	}

#	undef DRAW_MSG

	if (_drm_expose_raw(drm, frame) < 0) {
		_drm_cleanup(drm);
		_D_LOG_ERROR("Device destroyed due an error (expose)");
	}
	return (msg_drawn ? -1 : 0);
}

static void _drm_vsync_callback(int fd, uint n_frame, uint sec, uint usec, void *v_run) {
	(void)fd;
	(void)n_frame;
	(void)sec;
	(void)usec;
	us_drm_runtime_s *const run = v_run;
	run->has_vsync = true;
	_D_LOG_DEBUG("Got VSync signal");
}

static int _drm_expose_raw(us_drm_s *drm, const us_frame_s *frame) {
	us_drm_runtime_s *const run = drm->run;
	us_drm_buffer_s *const buf = &run->bufs[run->next_n_buf];

	_D_LOG_DEBUG("Exposing%s framebuffer n_buf=%u, vsync=%d ...",
		(frame == NULL ? " EMPTY" : ""), run->next_n_buf, run->has_vsync);

	if (frame == NULL) {
		memset(buf->data, 0, buf->allocated);
	} else {
		memcpy(buf->data, frame->data, US_MIN(frame->used, buf->allocated));
	}

	run->has_vsync = false;
	const int retval = drmModePageFlip(
		run->fd, run->crtc_id, buf->id,
		DRM_MODE_PAGE_FLIP_EVENT | DRM_MODE_PAGE_FLIP_ASYNC,
		run);
	run->next_n_buf = (run->next_n_buf + 1) % run->n_bufs;
	return retval;
}

static void _drm_cleanup(us_drm_s *drm) {
	us_drm_runtime_s *const run = drm->run;

	_D_LOG_DEBUG("Cleaning up ...");

	if (run->saved_crtc != NULL) {
		if (drmModeSetCrtc(run->fd,
			run->saved_crtc->crtc_id, run->saved_crtc->buffer_id,
			run->saved_crtc->x, run->saved_crtc->y,
			&run->conn_id, 1, &run->saved_crtc->mode
		) < 0 && errno != ENOENT) {
			_D_LOG_PERROR("Can't restore CRTC");
		}
		drmModeFreeCrtc(run->saved_crtc);
		run->saved_crtc = NULL;
	}

	if (run->bufs != NULL) {
		for (uint n_buf = 0; n_buf < run->n_bufs; ++n_buf) {
			us_drm_buffer_s *const buf = &run->bufs[n_buf];
			if (buf->data != NULL && munmap(buf->data, buf->allocated)) {
				_D_LOG_PERROR("Can't unmap buffer=%u", n_buf);
			}
			if (buf->fb_added && drmModeRmFB(run->fd, buf->id) < 0) {
				_D_LOG_PERROR("Can't remove buffer=%u", n_buf);
			}
			if (buf->dumb_created) {
				struct drm_mode_destroy_dumb destroy = {.handle = buf->handle};
				if (drmIoctl(run->fd, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy) < 0) {
					_D_LOG_PERROR("Can't destroy dumb buffer=%u", n_buf);
				}
			}
		}
		US_DELETE(run->bufs, free);
		run->n_bufs = 0;
	}

	US_CLOSE_FD(run->status_fd, close);
	US_CLOSE_FD(run->fd, close);

	run->crtc_id = 0;
	run->next_n_buf = 0;
	run->has_vsync = false;
	run->state = US_DRM_STATE_CLOSED;
}

static int _drm_ensure(us_drm_s *drm, const us_frame_s *frame, float hz) {
	us_drm_runtime_s *const run = drm->run;

	switch (_drm_check_status(drm)) {
		case 0: break;
		case -2: goto unplugged;
		default: goto error;
	}

	if (frame == NULL && run->state == US_DRM_STATE_OK) {
		return 0;
	} else if (
		frame != NULL
		&& run->p_width == frame->width
		&& run->p_height == frame->height
		&& run->p_hz == hz
		&& run->state <= US_DRM_STATE_CLOSED
	) {
		return (run->state == US_DRM_STATE_OK ? 0 : -1);
	}

	const us_drm_state_e saved_state = run->state;
	_drm_cleanup(drm);
	if (saved_state > US_DRM_STATE_CLOSED) {
		run->state = saved_state;
	}

	run->p_width = (frame != NULL ? frame->width : 0); // 0 for find the native resolution
	run->p_height = (frame != NULL ? frame->height : 0);
	run->p_hz = hz;

	_D_LOG_INFO("Configuring DRM device ...");

	if ((run->fd = open(drm->path, O_RDWR | O_CLOEXEC | O_NONBLOCK)) < 0) {
		_D_LOG_PERROR("Can't open DRM device");
		goto error;
	}

#	define CHECK_CAP(x_cap) { \
			u64 m_check; \
			if (drmGetCap(run->fd, x_cap, &m_check) < 0) { \
				_D_LOG_PERROR("Can't check " #x_cap); \
				goto error; \
			} \
			if (!m_check) { \
				_D_LOG_ERROR(#x_cap " is not supported"); \
				goto error; \
			} \
		}
	CHECK_CAP(DRM_CAP_DUMB_BUFFER);
	// CHECK_CAP(DRM_CAP_PRIME);
#	undef CHECK_CAP

	switch (_drm_find_sink(drm, run->p_width, run->p_height, run->p_hz)) {
		case 0: break;
		case -2: goto unplugged;
		default: goto error;
	}

	const float mode_hz = _get_refresh_rate(&run->mode);
	if (frame == NULL) {
		run->p_width = run->mode.hdisplay;
		run->p_height = run->mode.vdisplay;
		run->p_hz = mode_hz;
	}
	_D_LOG_INFO("Using %s mode: %ux%up%.02f",
		drm->port, run->mode.hdisplay, run->mode.vdisplay, mode_hz);

	if (_drm_init_buffers(drm) < 0) {
		goto error;
	}

	if (_drm_start_video(drm) < 0) {
		goto error;
	}

	_D_LOG_INFO("Showing ...");
	run->state = US_DRM_STATE_OK;
	return 0;

error:
	_drm_cleanup(drm);
	_D_LOG_ERROR("Device destroyed due an error (ensure)");
	return -1;

unplugged:
	if (run->state != US_DRM_STATE_NO_DISPLAY) {
		_D_LOG_INFO("Display %s unplugged", drm->port);
	}
	_drm_cleanup(drm);
	run->state = US_DRM_STATE_NO_DISPLAY;
	return -2;
}

static int _drm_check_status(us_drm_s *drm) {
	us_drm_runtime_s *run = drm->run;

	if (run->status_fd < 0) {
		struct stat st;
		if (stat(drm->path, &st) < 0) {
			_D_LOG_PERROR("Can't stat() DRM device");
			goto error;
		}
		const uint mi = minor(st.st_rdev);

		char path[128];
		US_SNPRINTF(path, 127, "/sys/class/drm/card%u-%s/status", mi, drm->port);
		if ((run->status_fd = open(path, O_RDONLY | O_CLOEXEC)) < 0) {
			_D_LOG_PERROR("Can't open DRM device status file: %s", path);
			goto error;
		}
	}

	char status_ch;
	if (read(run->status_fd, &status_ch, 1) != 1) {
		_D_LOG_PERROR("Can't read connector status");
		goto error;
	}
	if (lseek(run->status_fd, 0, SEEK_SET) != 0) {
		_D_LOG_PERROR("Can't rewind connector status");
		goto error;
	}
	return (status_ch == 'd' ? -2 : 0);

error:
	US_CLOSE_FD(run->status_fd, close);
	return -1;
}

static int _drm_find_sink(us_drm_s *drm, uint width, uint height, float hz) {
	us_drm_runtime_s *const run = drm->run;

	run->crtc_id = 0;

	_D_LOG_DEBUG("Trying to find the appropriate sink ...");

	drmModeRes *res = drmModeGetResources(run->fd);
	if (res == NULL) {
		_D_LOG_PERROR("Can't get resources info");
		goto done;
	}
	if (res->count_connectors <= 0) {
		_D_LOG_ERROR("Can't find any connectors");
		goto done;
	}

	for (int ci = 0; ci < res->count_connectors; ++ci) {
		drmModeConnector *conn = drmModeGetConnector(run->fd, res->connectors[ci]);
		if (conn == NULL) {
			_D_LOG_PERROR("Can't get connector index=%d", ci);
			goto done;
		}

		char port[32];
		US_SNPRINTF(port, 31, "%s-%u",
			_connector_type_to_string(conn->connector_type),
			conn->connector_type_id);
		if (strcmp(port, drm->port) != 0) {
			drmModeFreeConnector(conn);
			continue;
		}
		_D_LOG_DEBUG("Found connector for port %s: conn_type=%d, conn_type_id=%d",
			drm->port, conn->connector_type, conn->connector_type_id);

		if (conn->connection != DRM_MODE_CONNECTED) {
			_D_LOG_DEBUG("Display is not connected");
			drmModeFreeConnector(conn);
			goto done;
		}

		drmModeModeInfo *best = NULL;
		drmModeModeInfo *closest = NULL;
		drmModeModeInfo *pref = NULL;
		for (int mi = 0; mi < conn->count_modes; ++mi) {
			drmModeModeInfo *const mode = &conn->modes[mi];
			if (mode->flags & DRM_MODE_FLAG_INTERLACE) {
				continue; // Paranoia for size and discard interlaced
			}
			const float mode_hz = _get_refresh_rate(mode);
			if (mode->hdisplay == width && mode->vdisplay == height) {
				best = mode; // Any mode with exact resolution
				if (hz > 0 && mode_hz == hz) {
					break; // Exact mode with same freq
				}
			}
			if (mode->hdisplay == width && mode->vdisplay < height) {
				if (closest == NULL || _get_refresh_rate(closest) != hz) {
					closest = mode; // Something like 1920x1080p60 for 1920x1200p60 source
				}
			}
			if (pref == NULL && (mode->type & DRM_MODE_TYPE_PREFERRED)) {
				pref = mode; // Preferred mode if nothing is found
			}
		}
		if (best == NULL) { best = closest; }
		if (best == NULL) { best = pref; }
		if (best == NULL) { best = (conn->count_modes > 0 ? &conn->modes[0] : NULL); }
		if (best == NULL) {
			_D_LOG_ERROR("Can't find any appropriate resolutions");
			drmModeFreeConnector(conn);
			goto unplugged;
		}
		assert(best->hdisplay > 0);
		assert(best->vdisplay > 0);

		u32 taken_crtcs = 0; // Unused here
		if ((run->crtc_id = _find_crtc(run->fd, res, conn, &taken_crtcs)) == 0) {
			_D_LOG_ERROR("Can't find CRTC");
			drmModeFreeConnector(conn);
			goto done;
		}
		run->conn_id = conn->connector_id;
		memcpy(&run->mode, best, sizeof(drmModeModeInfo));

		drmModeFreeConnector(conn);
		break;
	}

done:
	drmModeFreeResources(res);
	return (run->crtc_id > 0 ? 0 : -1);

unplugged:
	drmModeFreeResources(res);
	return -2;
}

static int _drm_init_buffers(us_drm_s *drm) {
	us_drm_runtime_s *const run = drm->run;

	_D_LOG_DEBUG("Initializing %u buffers ...", drm->n_bufs);

	US_CALLOC(run->bufs, drm->n_bufs);
	for (run->n_bufs = 0; run->n_bufs < drm->n_bufs; ++run->n_bufs) {
		const uint n_buf = run->n_bufs;
		us_drm_buffer_s *const buf = &run->bufs[n_buf];

		struct drm_mode_create_dumb create = {
			.width = run->mode.hdisplay,
			.height = run->mode.vdisplay,
			.bpp = 24,
		};
		if (drmIoctl(run->fd, DRM_IOCTL_MODE_CREATE_DUMB, &create) < 0) {
			_D_LOG_PERROR("Can't create dumb buffer=%u", n_buf);
			return -1;
		}
		buf->handle = create.handle;
		buf->dumb_created = true;

		u32 handles[4] = {create.handle};
		u32 strides[4] = {create.pitch};
		u32 offsets[4] = {0};
		if (drmModeAddFB2(
			run->fd,
			run->mode.hdisplay, run->mode.vdisplay, DRM_FORMAT_RGB888,
			handles, strides, offsets, &buf->id, 0
		)) {
			_D_LOG_PERROR("Can't setup buffer=%u", n_buf);
			return -1;
		}
		buf->fb_added = true;

		struct drm_mode_map_dumb map = {.handle = create.handle};
		if (drmIoctl(run->fd, DRM_IOCTL_MODE_MAP_DUMB, &map) < 0) {
			_D_LOG_PERROR("Can't prepare dumb buffer=%u to mapping", n_buf);
			return -1;
		}

		if ((buf->data = mmap(
			NULL, create.size,
			PROT_READ | PROT_WRITE, MAP_SHARED,
			run->fd, map.offset
		)) == MAP_FAILED) {
			_D_LOG_PERROR("Can't map buffer=%u", n_buf);
			return -1;
		}
		memset(buf->data, 0, create.size);
		buf->allocated = create.size;
	}
	return 0;
}

static int _drm_start_video(us_drm_s *drm) {
	us_drm_runtime_s *const run = drm->run;
	run->saved_crtc = drmModeGetCrtc(run->fd, run->crtc_id);
	_D_LOG_DEBUG("Setting up CRTC ...");
	if (drmModeSetCrtc(run->fd, run->crtc_id, run->bufs[0].id, 0, 0, &run->conn_id, 1, &run->mode) < 0) {
		_D_LOG_PERROR("Can't set CRTC");
		return -1;
	}
	if (_drm_expose_raw(drm, NULL) < 0) {
		_D_LOG_PERROR("Can't flip the first page");
		return -1;
	}
	return 0;
}

static u32 _find_crtc(int fd, drmModeRes *res, drmModeConnector *conn, u32 *taken_crtcs) {
	for (int ei = 0; ei < conn->count_encoders; ++ei) {
		drmModeEncoder *enc = drmModeGetEncoder(fd, conn->encoders[ei]);
		if (enc == NULL) {
			continue;
		}
		for (int ci = 0; ci < res->count_crtcs; ++ci) {
			u32 bit = (1 << ci);
			if (!(enc->possible_crtcs & bit)) {
				continue; // Not compatible
			}
			if (*taken_crtcs & bit) {
				continue; // Already taken
			}
			drmModeFreeEncoder(enc);
			*taken_crtcs |= bit;
			return res->crtcs[ci];
		}
		drmModeFreeEncoder(enc);
	}
	return 0;
}

static const char *_connector_type_to_string(u32 type) {
	switch (type) {
#		define CASE_NAME(x_suffix, x_name) \
			case DRM_MODE_CONNECTOR_##x_suffix: return x_name;
		CASE_NAME(VGA,			"VGA");
		CASE_NAME(DVII,			"DVI-I");
		CASE_NAME(DVID,			"DVI-D");
		CASE_NAME(DVIA,			"DVI-A");
		CASE_NAME(Composite,	"Composite");
		CASE_NAME(SVIDEO,		"SVIDEO");
		CASE_NAME(LVDS,			"LVDS");
		CASE_NAME(Component,	"Component");
		CASE_NAME(9PinDIN,		"DIN");
		CASE_NAME(DisplayPort,	"DP");
		CASE_NAME(HDMIA,		"HDMI-A");
		CASE_NAME(HDMIB,		"HDMI-B");
		CASE_NAME(TV,			"TV");
		CASE_NAME(eDP,			"eDP");
		CASE_NAME(VIRTUAL,		"Virtual");
		CASE_NAME(DSI,			"DSI");
		CASE_NAME(DPI,			"DPI");
		CASE_NAME(WRITEBACK,	"Writeback");
		CASE_NAME(SPI,			"SPI");
		CASE_NAME(USB,			"USB");
		case DRM_MODE_CONNECTOR_Unknown: break;
#		undef CASE_NAME
	}
	return "Unknown";
}

static float _get_refresh_rate(const drmModeModeInfo *mode) {
	int mhz = (mode->clock * 1000000LL / mode->htotal + mode->vtotal / 2) / mode->vtotal;
	if (mode->flags & DRM_MODE_FLAG_INTERLACE) {
		mhz *= 2;
	}
	if (mode->flags & DRM_MODE_FLAG_DBLSCAN) {
		mhz /= 2;
	}
	if (mode->vscan > 1) {
		mhz /= mode->vscan;
	}
	return (float)mhz / 1000;
}
