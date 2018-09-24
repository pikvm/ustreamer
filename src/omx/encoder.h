#pragma once

#include <stdbool.h>

#include <IL/OMX_Component.h>
#include <interface/vcos/vcos_semaphore.h>

#include "../device.h"


struct omx_encoder_t {
	OMX_HANDLETYPE			encoder;
	OMX_BUFFERHEADERTYPE	*input_buffer;
	OMX_BUFFERHEADERTYPE	*output_buffer;
	bool					input_required;
	bool					output_available;
	bool					failed;
	VCOS_SEMAPHORE_T		handler_lock;

	bool	i_omx;
	bool	i_handler_lock;
	bool	i_encoder;
	bool	i_input_port_enabled;
	bool	i_output_port_enabled;
};


struct omx_encoder_t *omx_encoder_init();
void omx_encoder_destroy(struct omx_encoder_t *omx);

int omx_encoder_prepare(struct omx_encoder_t *omx, struct device_t *dev);
int omx_encoder_compress_buffer(struct omx_encoder_t *omx, struct device_t *dev, int index);
