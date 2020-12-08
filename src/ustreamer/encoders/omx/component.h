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

#include <string.h>
#include <unistd.h>

#include <IL/OMX_Core.h>
#include <IL/OMX_Component.h>

#include "../../../common/logging.h"

#include "formatters.h"


#define OMX_INIT_STRUCTURE(_var) { \
		memset(&(_var), 0, sizeof(_var)); \
		(_var).nSize = sizeof(_var); \
		(_var).nVersion.nVersion = OMX_VERSION; \
		(_var).nVersion.s.nVersionMajor = OMX_VERSION_MAJOR; \
		(_var).nVersion.s.nVersionMinor = OMX_VERSION_MINOR; \
		(_var).nVersion.s.nRevision = OMX_VERSION_REVISION; \
		(_var).nVersion.s.nStep = OMX_VERSION_STEP; \
	}


int component_enable_port(OMX_HANDLETYPE *component, OMX_U32 port);
int component_disable_port(OMX_HANDLETYPE *component, OMX_U32 port);

int component_get_portdef(OMX_HANDLETYPE *component, OMX_PARAM_PORTDEFINITIONTYPE *portdef, OMX_U32 port);
int component_set_portdef(OMX_HANDLETYPE *component, OMX_PARAM_PORTDEFINITIONTYPE *portdef);

int component_set_state(OMX_HANDLETYPE *component, OMX_STATETYPE state);
