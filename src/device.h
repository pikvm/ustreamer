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
#include <signal.h>

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
	void	*start;
	size_t	length;
};

struct picture_t {
	unsigned char	*data;
	size_t			size;
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

#define S_MANUAL(_dest)	int _dest; bool _dest##_set;
#define S_AUTO(_dest)	int _dest; bool _dest##_set; bool _dest##_auto;

struct image_settings_t {
	S_AUTO		(brightness);
	S_MANUAL	(contrast);
	S_MANUAL	(saturation);
	S_AUTO		(hue);
	S_MANUAL	(gamma);
	S_MANUAL	(sharpness);
	S_MANUAL	(backlight_compensation);
	S_AUTO		(white_balance);
	S_AUTO		(gain);
};

#undef S_AUTO
#undef S_MANUAL

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

	struct image_settings_t *img;

	struct device_runtime_t *run;
	sig_atomic_t volatile stop;
};


struct device_t *device_init();
void device_destroy(struct device_t *dev);

int device_parse_format(const char *str);
v4l2_std_id device_parse_standard(const char *str);

int device_open(struct device_t *dev);
void device_close(struct device_t *dev);
