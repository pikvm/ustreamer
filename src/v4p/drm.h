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


#pragma once


#include <xf86drmMode.h>

#include "../libs/types.h"
#include "../libs/frame.h"

#include "ftext.h"


typedef enum {
	US_DRM_EXPOSE_FRAME = 0,
	US_DRM_EXPOSE_NO_SIGNAL,
	US_DRM_EXPOSE_BUSY,
} us_drm_expose_e;

typedef enum {
	US_DRM_STATE_OK = 0,
	US_DRM_STATE_CLOSED,
	US_DRM_STATE_NO_DISPLAY,
} us_drm_state_e;

typedef struct {
	u32		id;
	u32		handle;
	u8		*data;
	uz		allocated;
	bool	dumb_created;
	bool	fb_added;
} us_drm_buffer_s;

typedef struct {
	int				status_fd;

	int				fd;
	u32				crtc_id;
	u32				conn_id;
	drmModeModeInfo	mode;
	us_drm_buffer_s	*bufs;
	uint			n_bufs;
	drmModeCrtc		*saved_crtc;
	uint			next_n_buf;
	bool			has_vsync;

	us_ftext_s		*ft;

	uint			p_width;
	uint			p_height;
	float			p_hz;

	us_drm_state_e	state;
} us_drm_runtime_s;

typedef struct {
	char	*path;
	char	*port;
	uint	n_bufs;
	uint	timeout;

	us_drm_runtime_s *run;
} us_drm_s;


us_drm_s *us_drm_init(void);
void us_drm_destroy(us_drm_s *drm);

int us_drm_wait_for_vsync(us_drm_s *drm);
int us_drm_expose(us_drm_s *drm, us_drm_expose_e ex, const us_frame_s *frame, float hz);
