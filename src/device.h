/*****************************************************************************
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


#define STANDARD_UNKNOWN	V4L2_STD_UNKNOWN
#define STANDARDS_STR		"UNKNOWN, PAL, NTSC, SECAM"

#define FORMAT_UNKNOWN	-1
#define FORMATS_STR		"YUYV, UYVY, RGB565"


struct hw_buffer_t {
	void	*start;
	size_t	length;
};

struct picture_t {
	unsigned char	*data;
	unsigned long	size;
	unsigned long	allocated;
};

struct device_runtime_t {
	int					fd;
	unsigned			width;
	unsigned			height;
	unsigned			format;
	unsigned			n_buffers;
	unsigned			n_workers;
	struct hw_buffer_t	*hw_buffers;
	struct picture_t	*pictures;
	unsigned long		max_picture_size;
	bool				capturing;
};

struct device_t {
	char			*path;
	unsigned		width;
	unsigned		height;
	unsigned		format;
	v4l2_std_id		standard;
	bool			dv_timings;
	unsigned		n_buffers;
	unsigned		n_workers;
	unsigned		every_frame;
	unsigned		min_frame_size;
	unsigned		jpeg_quality;
	unsigned		timeout;
	unsigned		error_timeout;

	struct device_runtime_t *run;
	sig_atomic_t volatile stop;
};


struct device_t *device_init();
void device_destroy(struct device_t *dev);

int device_parse_format(const char *const str);
v4l2_std_id device_parse_standard(const char *const str);

int device_open(struct device_t *dev);
void device_close(struct device_t *dev);
