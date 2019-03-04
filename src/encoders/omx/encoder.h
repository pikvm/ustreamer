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

#include <stdbool.h>

#include <IL/OMX_Component.h>
#include <interface/vcos/vcos_semaphore.h>

#include "../../device.h"


#define OMX_MAX_ENCODERS 3


struct omx_encoder_t {
	OMX_HANDLETYPE			encoder;
	OMX_BUFFERHEADERTYPE	*input_buffer;
	OMX_BUFFERHEADERTYPE	*output_buffer;
	bool					input_required;
	bool					output_available;
	bool					failed;
	VCOS_SEMAPHORE_T		handler_lock;

	bool	i_handler_lock;
	bool	i_encoder;
	bool	i_input_port_enabled;
	bool	i_output_port_enabled;
};


struct omx_encoder_t *omx_encoder_init();
void omx_encoder_destroy(struct omx_encoder_t *omx);

int omx_encoder_prepare_live(struct omx_encoder_t *omx, struct device_t *dev, unsigned quality);
int omx_encoder_compress_buffer(struct omx_encoder_t *omx, struct device_t *dev, unsigned index);
