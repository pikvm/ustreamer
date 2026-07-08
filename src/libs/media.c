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


#include "media.h"

#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>

#include <sys/sysmacros.h>

#include <linux/videodev2.h>
#include <linux/media.h>
#include <linux/v4l2-subdev.h>

#include "types.h"
#include "errors.h"
#include "tools.h"
#include "logging.h"
#include "xioctl.h"


#define _LOG_ERROR(x_msg, ...)	US_LOG_ERROR("CAP: " x_msg, ##__VA_ARGS__)
#define _LOG_PERROR(x_msg, ...)	US_LOG_PERROR("CAP: " x_msg, ##__VA_ARGS__)


int us_media_find_link(
	struct media_v2_topology *topology,
	u32 link_type,
	int source_id,
	int sink_id,
	u32 *link_flags
) {
	struct media_v2_link *links = (struct media_v2_link *)topology->ptr_links;

	for (uint i = 0; i < topology->num_links; ++i) {
		if ((links[i].flags & MEDIA_LNK_FL_LINK_TYPE) != link_type) {
			continue;
		}
		if (source_id >= 0 && (u32)source_id == links[i].source_id) {
			if (link_flags) {
				*link_flags = links[i].flags;
			}
			return links[i].sink_id;
		}
		if (sink_id >= 0 && (u32)sink_id == links[i].sink_id) {
			if (link_flags) {
				*link_flags = links[i].flags;
			}
			return links[i].source_id;
		}
	}
	return -1;
}

int us_media_find_entity_by_name(
	const struct media_v2_topology *topology,
	const char *name
) {
	const struct media_v2_entity *entities = (struct media_v2_entity *)topology->ptr_entities;

	for (uint i = 0; i < topology->num_entities; ++i) {
		if (!strcmp(entities[i].name, name)) {
			return entities[i].id;
		}
	}
	return -1;
}

int us_media_find_entity_by_devnode(
	struct media_v2_topology *topology,
	const dev_t devnode
) {
	const struct media_v2_interface *interfaces = (struct media_v2_interface *)topology->ptr_interfaces;

	for (uint i = 0; i < topology->num_interfaces; ++i) {
		if (
			interfaces[i].intf_type == MEDIA_INTF_T_V4L_VIDEO
			&& interfaces[i].devnode.major == major(devnode)
			&& interfaces[i].devnode.minor == minor(devnode)
		) {
			return us_media_find_link(
				topology, MEDIA_LNK_FL_INTERFACE_LINK,
				interfaces[i].id, -1, NULL);
		}
	}
	return -1;
}

const struct media_v2_pad *us_media_find_pad_by_entity(
	const struct media_v2_topology *topology,
	u32 pad_type,
	u32 entity_id
) {
	const struct media_v2_pad *pads = (const struct media_v2_pad *)topology->ptr_pads;

	for (uint i = 0; i < topology->num_pads; ++i) {
		if ((pads[i].flags & pad_type) && pads[i].entity_id == entity_id) {
			return &pads[i];
		}
	}
	return NULL;
}

const struct media_v2_pad *us_media_find_pad(
	const struct media_v2_topology *topology,
	u32 pad_type,
	u32 pad_id
) {
	const struct media_v2_pad *pads = (const struct media_v2_pad *)topology->ptr_pads;

	for (uint i = 0; i < topology->num_pads; ++i) {
		if ((pads[i].flags & pad_type) && pads[i].id == pad_id) {
			return &pads[i];
		}
	}
	return NULL;
}

int us_media_xioctl_setup_link(
	int fd,
	const struct media_v2_pad *source_pad,
	const struct media_v2_pad *sink_pad,
	u32 link_flags
) {
	struct media_link_desc link_desc = {
		.source = {
			.entity = source_pad->entity_id,
			.index = source_pad->index,
		},
		.sink = {
			.entity = sink_pad->entity_id,
			.index = sink_pad->index,
		},
		.flags = link_flags,
	};
	return us_xioctl(fd, MEDIA_IOC_SETUP_LINK, &link_desc);
}

struct media_v2_topology *us_media_topology_init(int fd) {
	struct media_v2_topology *topology = NULL;
	struct media_device_info dev_info = {0};

	if (us_xioctl(fd, MEDIA_IOC_DEVICE_INFO, &dev_info) < 0) {
		_LOG_PERROR("Can't to query media device info");
		goto error;
	}

	if (!MEDIA_V2_PAD_HAS_INDEX(dev_info.media_version)) {
		_LOG_ERROR("Media topology doesn't have pad indices, too old kernel?");
		goto error;
	}

	US_CALLOC(topology, 1);

	if (us_xioctl(fd, MEDIA_IOC_G_TOPOLOGY, topology) < 0) {
		_LOG_PERROR("Can't query media topology info");
		goto error;
	}

#	define INIT_FIELD(x_field, x_type) { \
			void *m_ptr = calloc(topology->num_##x_field, sizeof(x_type)); \
			US_A(m_ptr); \
			topology->ptr_##x_field = (uintptr_t)m_ptr; \
		}
	INIT_FIELD(entities, struct media_v2_entity);
	INIT_FIELD(links, struct media_v2_link);
	INIT_FIELD(interfaces, struct media_v2_interface);
	INIT_FIELD(pads, struct media_v2_pad);
#	undef INIT_FIELD

	if (us_xioctl(fd, MEDIA_IOC_G_TOPOLOGY, topology) < 0) {
		_LOG_PERROR("Failed to retrieve topology info");
		goto error;
	}

	return topology;

error:
	US_DELETE(topology, us_media_topology_destroy);
	return NULL;
}

void us_media_topology_destroy(struct media_v2_topology *topology) {
#	define DELETE_FIELD(x_field) { \
			if (topology->ptr_##x_field) { \
				free((void *)topology->ptr_##x_field); \
			} \
		}
	DELETE_FIELD(pads);
	DELETE_FIELD(interfaces);
	DELETE_FIELD(links);
	DELETE_FIELD(entities);
#	undef DELETE_FIELD
	free(topology);
}
