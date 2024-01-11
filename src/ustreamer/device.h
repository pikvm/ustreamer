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

#include <stdlib.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
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

#include "../libs/tools.h"
#include "../libs/array.h"
#include "../libs/logging.h"
#include "../libs/threading.h"
#include "../libs/frame.h"
#include "../libs/xioctl.h"


#define US_VIDEO_MIN_WIDTH		((unsigned)160)
#define US_VIDEO_MAX_WIDTH		((unsigned)15360)

#define US_VIDEO_MIN_HEIGHT		((unsigned)120)
#define US_VIDEO_MAX_HEIGHT		((unsigned)8640)

#define US_VIDEO_MAX_FPS		((unsigned)120)

#define US_STANDARD_UNKNOWN		V4L2_STD_UNKNOWN
#define US_STANDARDS_STR		"PAL, NTSC, SECAM"

#define US_FORMAT_UNKNOWN		-1
#define US_FORMATS_STR			"YUYV, YVYU, UYVY, RGB565, RGB24, BGR24, MJPEG, JPEG"

#define US_IO_METHOD_UNKNOWN	-1
#define US_IO_METHODS_STR		"MMAP, USERPTR"


typedef struct {
	us_frame_s			raw;
	struct v4l2_buffer	buf;
	int					dma_fd;
	bool				grabbed;
} us_hw_buffer_s;

typedef struct {
	int					fd;
	unsigned			width;
	unsigned			height;
	unsigned			format;
	unsigned			stride;
	unsigned			hw_fps;
	unsigned			jpeg_quality;
	size_t				raw_size;
	unsigned			n_bufs;
	us_hw_buffer_s		*hw_bufs;
	enum v4l2_buf_type	capture_type;
	bool				capturing;
	bool				persistent_timeout_reported;
} us_device_runtime_s;

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
	unsigned			input;
	unsigned			width;
	unsigned			height;
	unsigned			format;
	unsigned			jpeg_quality;
	v4l2_std_id			standard;
	enum v4l2_memory	io_method;
	bool				dv_timings;
	unsigned			n_bufs;
	unsigned			desired_fps;
	size_t				min_frame_size;
	bool				persistent;
	unsigned			timeout;
	us_controls_s 		ctl;
	us_device_runtime_s *run;
} us_device_s;


us_device_s *us_device_init(void);
void us_device_destroy(us_device_s *dev);

int us_device_parse_format(const char *str);
v4l2_std_id us_device_parse_standard(const char *str);
int us_device_parse_io_method(const char *str);

int us_device_open(us_device_s *dev);
void us_device_close(us_device_s *dev);

int us_device_export_to_dma(us_device_s *dev);
int us_device_switch_capturing(us_device_s *dev, bool enable);
int us_device_select(us_device_s *dev, bool *has_read, bool *has_write, bool *has_error);
int us_device_grab_buffer(us_device_s *dev, us_hw_buffer_s **hw);
int us_device_release_buffer(us_device_s *dev, us_hw_buffer_s *hw);
int us_device_consume_event(us_device_s *dev);
