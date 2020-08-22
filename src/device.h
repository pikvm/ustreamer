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


#pragma once

#include <stddef.h>
#include <stdbool.h>

#include <linux/videodev2.h>

#include "picture.h"


#define VIDEO_MIN_WIDTH		((unsigned)160)
#define VIDEO_MAX_WIDTH		((unsigned)10240)

#define VIDEO_MIN_HEIGHT	((unsigned)120)
#define VIDEO_MAX_HEIGHT	((unsigned)4320)

#define VIDEO_MAX_FPS		((unsigned)120)

#define STANDARD_UNKNOWN	V4L2_STD_UNKNOWN
#define STANDARDS_STR		"PAL, NTSC, SECAM"

#define FORMAT_UNKNOWN	-1
#define FORMATS_STR		"YUYV, UYVY, RGB565, RGB24, JPEG"

#define IO_METHOD_UNKNOWN	-1
#define IO_METHODS_STR		"MMAP, USERPTR"


struct hw_buffer_t {
	unsigned char		*data;
	size_t				used;
	size_t				allocated;
	struct v4l2_buffer	buf_info;
};

struct device_runtime_t {
	int					fd;
	unsigned			width;
	unsigned			height;
	unsigned			format;
	unsigned			hw_fps;
	size_t				raw_size;
	unsigned			n_buffers;
	unsigned			n_workers;
	struct hw_buffer_t	*hw_buffers;
	struct picture_t	**pictures;
	bool				capturing;
};

enum control_mode_t {
	CTL_MODE_NONE = 0,
	CTL_MODE_VALUE,
	CTL_MODE_AUTO,
	CTL_MODE_DEFAULT,
};

struct control_t {
	enum control_mode_t	mode;
	int					value;
};

struct controls_t {
	struct control_t brightness;
	struct control_t contrast;
	struct control_t saturation;
	struct control_t hue;
	struct control_t gamma;
	struct control_t sharpness;
	struct control_t backlight_compensation;
	struct control_t white_balance;
	struct control_t gain;
	struct control_t color_effect;
	struct control_t flip_vertical;
	struct control_t flip_horizontal;
};

struct device_t {
	char			*path;
	unsigned		input;
	unsigned		width;
	unsigned		height;
	unsigned		format;
	v4l2_std_id		standard;
	enum v4l2_memory	io_method;
	bool			dv_timings;
	unsigned		n_buffers;
	unsigned		n_workers;
	unsigned		desired_fps;
	size_t			min_frame_size;
	bool			persistent;
	unsigned		timeout;
	unsigned		error_delay;

	struct controls_t ctl;

	struct device_runtime_t *run;
};


struct device_t *device_init(void);
void device_destroy(struct device_t *dev);

int device_parse_format(const char *str);
v4l2_std_id device_parse_standard(const char *str);
int device_parse_io_method(const char *str);

int device_open(struct device_t *dev);
void device_close(struct device_t *dev);

int device_switch_capturing(struct device_t *dev, bool enable);
int device_select(struct device_t *dev, bool *has_read, bool *has_write, bool *has_error);
int device_grab_buffer(struct device_t *dev);
int device_release_buffer(struct device_t *dev, unsigned index);
int device_consume_event(struct device_t *dev);
