#pragma once

#include <stddef.h>
#include <stdbool.h>
#include <linux/videodev2.h>


#define FORMAT_UNKNOWN		-1
#define STANDARD_UNKNOWN	V4L2_STD_UNKNOWN


struct buffer_t {
	void	*start;
	size_t	length;
};

struct device_runtime_t {
	int				fd;
	unsigned		width;
	unsigned		height;
	unsigned		format;
	unsigned		n_buffers;
	struct buffer_t	*buffers;
	unsigned char	**pictures;
	bool			capturing;
};

struct device_t {
	char			*path;
	unsigned		width;
	unsigned		height;
	unsigned		format;
	v4l2_std_id		standard;
	bool			dv_timings;
	unsigned		n_buffers;
	unsigned		every_frame;
	unsigned		min_frame_size;
	unsigned		jpeg_quality;
	unsigned		timeout;
	unsigned		error_timeout;

	struct device_runtime_t	*run;
};


void device_init(struct device_t *dev, struct device_runtime_t *run);

int device_parse_format(const char *const str);
v4l2_std_id device_parse_standard(const char *const str);

int device_open(struct device_t *dev);
void device_close(struct device_t *dev);
