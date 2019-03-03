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


#include <string.h>
#include <assert.h>

#include <linux/videodev2.h>

#include "../tools.h"
#include "../logging.h"
#include "../xioctl.h"
#include "../device.h"


int hw_encoder_prepare_live(struct device_t *dev, unsigned quality) {
	struct v4l2_jpegcompression comp;

	MEMSET_ZERO(comp);

	if (xioctl(dev->run->fd, VIDIOC_G_JPEGCOMP, &comp) < 0) {
		LOG_ERROR("Can't query HW JPEG compressor params and set quality (unsupported)");
		return -1;
	}
	comp.quality = quality;
	if (xioctl(dev->run->fd, VIDIOC_S_JPEGCOMP, &comp) < 0) {
		LOG_ERROR("Can't set HW JPEG compressor quality (unsopported)");
		return -1;
	}
	return 0;
}

void hw_encoder_compress_buffer(struct device_t *dev, unsigned index) {
	if (dev->run->format != V4L2_PIX_FMT_MJPEG && dev->run->format != V4L2_PIX_FMT_JPEG) {
		assert(0 && "Unsupported input format for HW JPEG compressor");
	}
	assert(dev->run->pictures[index].allocated >= dev->run->hw_buffers[index].length);
	memcpy(dev->run->pictures[index].data, dev->run->hw_buffers[index].start, dev->run->hw_buffers[index].length);
	dev->run->pictures[index].size = dev->run->hw_buffers[index].length;
}
