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


#pragma once


#include <xf86drmMode.h>

#include "../types.h"
#include "../frame.h"
#include "../frametext.h"
#include "../capture.h"


typedef enum {
	US_DRM_STUB_USER = 1,
	US_DRM_STUB_BAD_RESOLUTION,
	US_DRM_STUB_BAD_FORMAT,
	US_DRM_STUB_NO_SIGNAL,
	US_DRM_STUB_BUSY,
} us_drm_stub_e;

typedef struct {
	u32		id;
	u32		handle;
	u8		*data;
	uz		allocated;
	bool	dumb_created;
	bool	fb_added;
	struct {
		bool	*has_vsync;
		int		*exposing_dma_fd;
	} ctx;
} us_drm_buffer_s;

typedef struct {
	int				status_fd;
	int				fd;
	u32				crtc_id;
	u32				conn_id;
	u32				dpms_id;
	drmModeModeInfo	mode;
	us_drm_buffer_s	*bufs;
	uint			n_bufs;
	drmModeCrtc		*saved_crtc;
	int				dpms_state;
	int				opened;

	bool			has_vsync;
	int				exposing_dma_fd;
	uint			stub_n_buf;
	ldf				blank_at_ts;

	int				once;
	us_frametext_s	*ft;
} us_drm_runtime_s;

typedef struct {
	char	*path;
	char	*port;
	uint	timeout;
	uint	blank_after;

	us_drm_runtime_s *run;
} us_drm_s;


us_drm_s *us_drm_init(const char* port_name);
void us_drm_destroy(us_drm_s *drm);

int us_drm_open(us_drm_s *drm, const us_capture_s *cap);
void us_drm_close(us_drm_s *drm);

int us_drm_dpms_power_off(us_drm_s *drm);
int us_drm_wait_for_vsync(us_drm_s *drm);
int us_drm_expose_stub(us_drm_s *drm, us_drm_stub_e stub, const us_capture_s *cap);
int us_drm_expose_dma(us_drm_s *drm, const us_capture_hwbuf_s *hw);
int us_drm_ensure_no_signal(us_drm_s *drm);
