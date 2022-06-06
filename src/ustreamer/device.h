/*****************************************************************************
#                                                                            #
#    uStreamer - Lightweight and fast MJPEG-HTTP streamer.                   #
#                                                                            #
#    Copyright (C) 2018-2022  Maxim Devaev <mdevaev@gmail.com>               #
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
#include "../libs/logging.h"
#include "../libs/threading.h"
#include "../libs/frame.h"
#include "../libs/xioctl.h"


#define VIDEO_MIN_WIDTH		((unsigned)160)
#define VIDEO_MAX_WIDTH		((unsigned)10240)

#define VIDEO_MIN_HEIGHT	((unsigned)120)
#define VIDEO_MAX_HEIGHT	((unsigned)4320)

#define VIDEO_MAX_FPS		((unsigned)120)

#define STANDARD_UNKNOWN	V4L2_STD_UNKNOWN
#define STANDARDS_STR		"PAL, NTSC, SECAM"

#define FORMAT_UNKNOWN	-1
#define FORMATS_STR		"YUYV, UYVY, RGB565, RGB24, MJPEG, JPEG"

#define IO_METHOD_UNKNOWN	-1
#define IO_METHODS_STR		"MMAP, USERPTR"


typedef struct {
	frame_s				raw;
	struct v4l2_buffer	buf;
	int					dma_fd;
	bool				grabbed;
} hw_buffer_s;

typedef struct {
	int			fd;
	unsigned	width;
	unsigned	height;
	unsigned	format;
	unsigned	stride;
	unsigned	hw_fps;
	unsigned	jpeg_quality;
	size_t		raw_size;
	unsigned	n_bufs;
	hw_buffer_s	*hw_bufs;
	bool		capturing;
	bool		persistent_timeout_reported;
} device_runtime_s;

typedef enum {
	CTL_MODE_NONE = 0,
	CTL_MODE_VALUE,
	CTL_MODE_AUTO,
	CTL_MODE_DEFAULT,
} control_mode_e;

typedef struct {
	control_mode_e	mode;
	int				value;
} control_s;

typedef struct {
	control_s brightness;
	control_s contrast;
	control_s saturation;
	control_s hue;
	control_s gamma;
	control_s sharpness;
	control_s backlight_compensation;
	control_s white_balance;
	control_s gain;
	control_s color_effect;
	control_s rotate;
	control_s flip_vertical;
	control_s flip_horizontal;
} controls_s;

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

	controls_s ctl;

	device_runtime_s *run;
} device_s;


device_s *device_init(void);
void device_destroy(device_s *dev);

int device_parse_format(const char *str);
v4l2_std_id device_parse_standard(const char *str);
int device_parse_io_method(const char *str);

int device_open(device_s *dev);
void device_close(device_s *dev);

int device_export_to_dma(device_s *dev);
int device_switch_capturing(device_s *dev, bool enable);
int device_select(device_s *dev, bool *has_read, bool *has_write, bool *has_error);
int device_grab_buffer(device_s *dev, hw_buffer_s **hw);
int device_release_buffer(device_s *dev, hw_buffer_s *hw);
int device_consume_event(device_s *dev);
