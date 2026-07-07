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

#include <linux/videodev2.h>
#include <linux/media.h>
#include <linux/v4l2-subdev.h>

#include "types.h"


int us_media_find_link(
	struct media_v2_topology *topology,
	u32 link_type,
	int source_id,
	int sink_id,
	u32 *link_flags);

int us_media_find_entity_by_name(
	const struct media_v2_topology *topology,
	const char *name);

int us_media_find_entity_by_devnode(
	struct media_v2_topology *topology,
	const dev_t devnode);

const struct media_v2_pad *us_media_find_pad_by_entity(
	const struct media_v2_topology *topology,
	u32 pad_type,
	u32 entity_id);

const struct media_v2_pad *us_media_find_pad(
	const struct media_v2_topology *topology,
	u32 pad_type,
	u32 pad_id);

int us_media_xioctl_setup_link(
	int fd,
	const struct media_v2_pad *source_pad,
	const struct media_v2_pad *sink_pad,
	u32 link_flags);

struct media_v2_topology *us_media_topology_init(int fd);
void us_media_topology_destroy(struct media_v2_topology *topology);
