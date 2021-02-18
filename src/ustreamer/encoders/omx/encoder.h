/*****************************************************************************
#                                                                            #
#    uStreamer - Lightweight and fast MJPG-HTTP streamer.                    #
#                                                                            #
#    Copyright (C) 2018-2021  Maxim Devaev <mdevaev@gmail.com>               #
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
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>

#include <linux/videodev2.h>

#include <IL/OMX_Core.h>
#include <IL/OMX_Component.h>
#include <IL/OMX_Broadcom.h>
#include <interface/vcos/vcos_semaphore.h>

#include "../../../libs/tools.h"
#include "../../../libs/logging.h"
#include "../../../libs/frame.h"

#include "vcos.h"
#include "formatters.h"
#include "component.h"


#ifndef CFG_OMX_MAX_ENCODERS
#	define CFG_OMX_MAX_ENCODERS 3 // Raspberry Pi limitation
#endif
#define OMX_MAX_ENCODERS ((unsigned)(CFG_OMX_MAX_ENCODERS))


typedef struct {
	OMX_HANDLETYPE			comp;
	OMX_BUFFERHEADERTYPE	*input_buf;
	OMX_BUFFERHEADERTYPE	*output_buf;
	bool					input_required;
	bool					output_available;
	bool					failed;
	VCOS_SEMAPHORE_T		handler_sem;

	bool	i_handler_sem;
	bool	i_encoder;
	bool	i_input_port_enabled;
	bool	i_output_port_enabled;
} omx_encoder_s;


omx_encoder_s *omx_encoder_init(void);
void omx_encoder_destroy(omx_encoder_s *omx);

int omx_encoder_prepare(omx_encoder_s *omx, const frame_s *frame, unsigned quality);
int omx_encoder_compress(omx_encoder_s *omx, const frame_s *src, frame_s *dest);
