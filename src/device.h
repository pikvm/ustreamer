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


#define VIDEO_MIN_WIDTH		320
#define VIDEO_MAX_WIDTH		1920

#define VIDEO_MIN_HEIGHT	180
#define VIDEO_MAX_HEIGHT	1200

#define STANDARD_UNKNOWN	V4L2_STD_UNKNOWN
#define STANDARDS_STR		"PAL, NTSC, SECAM"

#define FORMAT_UNKNOWN	-1
#define FORMATS_STR		"YUYV, UYVY, RGB565, RGB24, JPEG"


struct hw_buffer_t {
	unsigned char		*data;
	size_t				used;
	size_t				allocated;
	struct v4l2_buffer	buf_info;
};

struct picture_t {
	unsigned char	*data;
	size_t			used;
	size_t			allocated;
	long double		grab_time;
	long double		encode_begin_time;
	long double		encode_end_time;
};

struct device_runtime_t {
	int					fd;
	unsigned			width;
	unsigned			height;
	unsigned			format;
	unsigned			n_buffers;
//	unsigned			n_workers; // FIXME
	struct hw_buffer_t	*hw_buffers;
	struct picture_t	*pictures;
	size_t				max_picture_size;
	bool				capturing;
};

struct control_t {
	int		value;
	bool	value_set;
	bool	auto_set;
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
};

struct device_t {
	char			*path;
	unsigned		input;
	unsigned		width;
	unsigned		height;
	unsigned		format;
	v4l2_std_id		standard;
	bool			dv_timings;
	unsigned		n_buffers;
	unsigned		n_workers;
	unsigned		desired_fps;
	size_t			min_frame_size;
	bool			persistent;
	unsigned		timeout;
	unsigned		error_delay;

	struct controls_t *ctl;

	struct device_runtime_t *run;
};


struct device_t *device_init();
void device_destroy(struct device_t *dev);

int device_parse_format(const char *str);
v4l2_std_id device_parse_standard(const char *str);

int device_open(struct device_t *dev);
void device_close(struct device_t *dev);

int device_switch_capturing(struct device_t *dev, bool enable);
int device_grab_buffer(struct device_t *dev);
int device_release_buffer(struct device_t *dev, unsigned index);
int device_consume_event(struct device_t *dev);
