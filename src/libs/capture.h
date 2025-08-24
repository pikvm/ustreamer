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

#include <stdatomic.h>

#include <linux/videodev2.h>

#include "types.h"
#include "frame.h"


#define US_VIDEO_MIN_WIDTH		((uint)160)
#define US_VIDEO_MAX_WIDTH		((uint)15360) // Remember about stream->run->http_capture_state;

#define US_VIDEO_MIN_HEIGHT		((uint)120)
#define US_VIDEO_MAX_HEIGHT		((uint)8640)

#define US_VIDEO_MAX_FPS		((uint)120)

#define US_STANDARDS_STR		"PAL, NTSC, SECAM"
#define US_FORMATS_STR			"NV12, NV16, NV24, YUYV, YVYU, UYVY, YUV420, YVU420, RGB565, RGB24, BGR24, GREY, MJPEG, JPEG"
#define US_IO_METHODS_STR		"MMAP, USERPTR"


typedef struct {
	us_frame_s			raw;
	struct v4l2_buffer	buf;
	int					dma_fd;
	bool				grabbed;
	atomic_int			refs;
} us_capture_hwbuf_s;

typedef struct {
	int					fd;
	uint				width;
	uint				height;
	uint				format;
	uint				stride;
	float				hz;
	uint				hw_fps;
	uint				jpeg_quality;
	uz					raw_size;
	uint				n_bufs;
	us_capture_hwbuf_s	*bufs;
	bool				dma;
	enum v4l2_buf_type	capture_type;
	bool				capture_mplane;
	bool				streamon;
	int					open_error_once;
} us_capture_runtime_s;

typedef enum {
	CTL_MODE_NONE = 0,
	CTL_MODE_VALUE,
	CTL_MODE_AUTO,
	CTL_MODE_DEFAULT,
} us_control_mode_e;

typedef struct {
	us_control_mode_e	mode;
	int					value;
} us_control_s;

typedef struct {
	us_control_s	brightness;
	us_control_s	contrast;
	us_control_s	saturation;
	us_control_s	hue;
	us_control_s	gamma;
	us_control_s	sharpness;
	us_control_s	backlight_compensation;
	us_control_s	white_balance;
	us_control_s	gain;
	us_control_s	color_effect;
	us_control_s	rotate;
	us_control_s	flip_vertical;
	us_control_s	flip_horizontal;
} us_controls_s;

typedef struct {
	char				*path;
	uint				input;
	uint				width;
	uint				height;
	uint				format;

	bool				format_swap_rgb;
	uint				jpeg_quality;
	v4l2_std_id			standard;
	enum v4l2_memory	io_method;
	bool				dv_timings;
	uint				n_bufs;
	bool				dma_export;
	bool				dma_required;
	uint				desired_fps;
	uz					min_frame_size;
	bool				allow_truncated_frames;
	bool				persistent;
	uint				timeout;
	us_controls_s 		ctl;
	us_capture_runtime_s *run;
} us_capture_s;


us_capture_s *us_capture_init(void);
void us_capture_destroy(us_capture_s *cap);

int us_capture_parse_format(const char *str);
int us_capture_parse_standard(const char *str);
int us_capture_parse_io_method(const char *str);

int us_capture_open(us_capture_s *cap);
void us_capture_close(us_capture_s *cap);

int us_capture_hwbuf_grab(us_capture_s *cap, us_capture_hwbuf_s **hw);
int us_capture_hwbuf_release(const us_capture_s *cap, us_capture_hwbuf_s *hw);

void us_capture_hwbuf_incref(us_capture_hwbuf_s *hw);
void us_capture_hwbuf_decref(us_capture_hwbuf_s *hw);
