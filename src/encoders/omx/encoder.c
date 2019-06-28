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


#include "encoder.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>

#include <linux/videodev2.h>

#include <bcm_host.h>
#include <IL/OMX_Core.h>
#include <IL/OMX_Component.h>
#include <IL/OMX_Broadcom.h>
#include <interface/vcos/vcos_semaphore.h>

#include "../../logging.h"
#include "../../tools.h"
#include "../../device.h"

#include "formatters.h"
#include "component.h"


static const OMX_U32 _INPUT_PORT = 340;
static const OMX_U32 _OUTPUT_PORT = 341;


static int _i_omx = 0;


static int _omx_init_component(struct omx_encoder_t *omx);
static int _omx_init_disable_ports(struct omx_encoder_t *omx);
static int _omx_setup_input(struct omx_encoder_t *omx, struct device_t *dev);
static int _omx_setup_output(struct omx_encoder_t *omx, unsigned quality);
static int _omx_encoder_clear_ports(struct omx_encoder_t *omx);

static OMX_ERRORTYPE _omx_event_handler(
	UNUSED OMX_HANDLETYPE encoder,
	OMX_PTR v_omx, OMX_EVENTTYPE event, OMX_U32 data1,
	UNUSED OMX_U32 data2, UNUSED OMX_PTR event_data);

static OMX_ERRORTYPE _omx_input_required_handler(
	UNUSED OMX_HANDLETYPE encoder,
	OMX_PTR v_omx, UNUSED OMX_BUFFERHEADERTYPE *buffer);

static OMX_ERRORTYPE _omx_output_available_handler(
	UNUSED OMX_HANDLETYPE encoder,
	OMX_PTR v_omx, UNUSED OMX_BUFFERHEADERTYPE *buffer);


struct omx_encoder_t *omx_encoder_init(void) {
	// Some theory:
	//   - http://www.fourcc.org/yuv.php
	//   - https://kwasi-ich.de/blog/2017/11/26/omx/
	//   - https://github.com/hopkinskong/rpi-omx-jpeg-encode/blob/master/jpeg_bench.cpp
	//   - https://github.com/kwasmich/OMXPlayground/blob/master/omxJPEGEnc.c
	//   - https://github.com/gagle/raspberrypi-openmax-jpeg/blob/master/jpeg.c
	//   - https://www.raspberrypi.org/forums/viewtopic.php?t=154790
	//   - https://bitbucket.org/bensch128/omxjpegencode/src/master/jpeg_encoder.cpp
	//   - http://home.nouwen.name/RaspberryPi/documentation/ilcomponents/image_encode.html

	struct omx_encoder_t *omx;
	OMX_ERRORTYPE error;

	A_CALLOC(omx, 1);

	assert(_i_omx >= 0);
	if (_i_omx == 0) {
		LOG_INFO("Initializing BCM ...");
		bcm_host_init();

		LOG_INFO("Initializing OMX ...");
		if ((error = OMX_Init()) != OMX_ErrorNone) {
			LOG_OMX_ERROR(error, "Can't initialize OMX");
			goto error;
		}
	}
	_i_omx += 1;

	LOG_INFO("Initializing OMX JPEG encoder ...");

	if (vcos_semaphore_create(&omx->handler_lock, "handler_lock", 0) != VCOS_SUCCESS) {
		LOG_ERROR("Can't create VCOS semaphore");
		goto error;
	}
	omx->i_handler_lock = true;

	if (_omx_init_component(omx) < 0) {
		goto error;
	}

	if (_omx_init_disable_ports(omx) < 0) {
		goto error;
	}

	return omx;

	error:
		omx_encoder_destroy(omx);
		return NULL;
}

void omx_encoder_destroy(struct omx_encoder_t *omx) {
	OMX_ERRORTYPE error;

	LOG_INFO("Destroying OMX JPEG encoder ...");

	component_set_state(&omx->encoder, OMX_StateIdle);
	_omx_encoder_clear_ports(omx);
	component_set_state(&omx->encoder, OMX_StateLoaded);

	if (omx->i_handler_lock) {
		vcos_semaphore_delete(&omx->handler_lock);
	}

	if (omx->i_encoder) {
		if ((error = OMX_FreeHandle(omx->encoder)) != OMX_ErrorNone) {
			LOG_OMX_ERROR(error, "Can't free OMX.broadcom.image_encode");
		}
	}

	assert(_i_omx >= 0);
	_i_omx -= 1;
	if (_i_omx == 0) {
		LOG_INFO("Destroying OMX ...");
		OMX_Deinit();

		LOG_INFO("Destroying BCM ...");
		bcm_host_deinit();
	}

	free(omx);
}

int omx_encoder_prepare(struct omx_encoder_t *omx, struct device_t *dev, unsigned quality) {
	if (component_set_state(&omx->encoder, OMX_StateIdle) < 0) {
		return -1;
	}
	if (_omx_encoder_clear_ports(omx) < 0) {
		return -1;
	}
	if (_omx_setup_input(omx, dev) < 0) {
		return -1;
	}
	if (_omx_setup_output(omx, quality) < 0) {
		return -1;
	}
	if (component_set_state(&omx->encoder, OMX_StateExecuting) < 0) {
		return -1;
	}
	return 0;
}

int omx_encoder_compress_buffer(struct omx_encoder_t *omx, struct device_t *dev, unsigned index) {
#	define PICTURE(_next)	dev->run->pictures[index]._next
#	define HW_BUFFER(_next)	dev->run->hw_buffers[index]._next
#	define IN(_next)		omx->input_buffer->_next
#	define OUT(_next)		omx->output_buffer->_next

	OMX_ERRORTYPE error;
	size_t slice_size = (IN(nAllocLen) < HW_BUFFER(used) ? IN(nAllocLen) : HW_BUFFER(used));
	size_t pos = 0;

	if ((error = OMX_FillThisBuffer(omx->encoder, omx->output_buffer)) != OMX_ErrorNone) {
		LOG_OMX_ERROR(error, "Failed to request filling of the output buffer on encoder");
		return -1;
	}

	PICTURE(used) = 0;
	omx->output_available = false;
	omx->input_required = true;

	while (true) {
		if (omx->failed) {
			return -1;
		}

		if (omx->output_available) {
			omx->output_available = false;

			assert(PICTURE(used) + OUT(nFilledLen) <= PICTURE(allocated));
			memcpy(PICTURE(data) + PICTURE(used), OUT(pBuffer) + OUT(nOffset), OUT(nFilledLen));
			PICTURE(used) += OUT(nFilledLen);

			if (OUT(nFlags) & OMX_BUFFERFLAG_ENDOFFRAME) {
				OUT(nFlags) = 0;
				break;
			}

			if ((error = OMX_FillThisBuffer(omx->encoder, omx->output_buffer)) != OMX_ErrorNone) {
				LOG_OMX_ERROR(error, "Failed to request filling of the output buffer on encoder");
				return -1;
			}
		}

		if (omx->input_required) {
			omx->input_required = false;

			if (pos == HW_BUFFER(used)) {
				continue;
			}

			memcpy(IN(pBuffer), HW_BUFFER(data) + pos, slice_size);
			IN(nOffset) = 0;
			IN(nFilledLen) = slice_size;

			pos += slice_size;

			if (pos + slice_size > HW_BUFFER(used)) {
				slice_size = HW_BUFFER(used) - pos;
			}

			if ((error = OMX_EmptyThisBuffer(omx->encoder, omx->input_buffer)) != OMX_ErrorNone) {
				LOG_OMX_ERROR(error, "Failed to request emptying of the input buffer on encoder");
				return -1;
			}
		}

		vcos_semaphore_wait(&omx->handler_lock);
	}

#	undef OUT
#	undef IN
#	undef HW_BUFFER
#	undef PICTURE
	return 0;
}

static int _omx_init_component(struct omx_encoder_t *omx) {
	OMX_ERRORTYPE error;

	OMX_CALLBACKTYPE callbacks;
	MEMSET_ZERO(callbacks);
	callbacks.EventHandler = _omx_event_handler;
	callbacks.EmptyBufferDone = _omx_input_required_handler;
	callbacks.FillBufferDone = _omx_output_available_handler;

	LOG_DEBUG("Initializing OMX.broadcom.image_encode ...");
	if ((error = OMX_GetHandle(&omx->encoder, "OMX.broadcom.image_encode", omx, &callbacks)) != OMX_ErrorNone) {
		LOG_OMX_ERROR(error, "Can't initialize OMX.broadcom.image_encode");
		return -1;
	}
	omx->i_encoder = true;
	return 0;
}

static int _omx_init_disable_ports(struct omx_encoder_t *omx) {
	OMX_ERRORTYPE error;
	OMX_INDEXTYPE types[] = {
		OMX_IndexParamAudioInit, OMX_IndexParamVideoInit,
		OMX_IndexParamImageInit, OMX_IndexParamOtherInit,
	};
	OMX_PORT_PARAM_TYPE ports;

	OMX_INIT_STRUCTURE(ports);
	if ((error = OMX_GetParameter(omx->encoder, OMX_IndexParamImageInit, &ports)) != OMX_ErrorNone) {
		LOG_OMX_ERROR(error, "Can't OMX_GetParameter(OMX_IndexParamImageInit)");
		return -1;
	}

	for (unsigned index = 0; index < 4; ++index) {
		if ((error = OMX_GetParameter(omx->encoder, types[index], &ports)) != OMX_ErrorNone) {
			LOG_OMX_ERROR(error, "Can't OMX_GetParameter(types[%u])", index);
			return -1;
		}
		for (OMX_U32 port = ports.nStartPortNumber; port < ports.nStartPortNumber + ports.nPorts; ++port) {
			if (component_disable_port(&omx->encoder, port) < 0) {
				return -1;
			}
		}
	}
	return 0;
}

static int _omx_setup_input(struct omx_encoder_t *omx, struct device_t *dev) {
	OMX_ERRORTYPE error;
	OMX_PARAM_PORTDEFINITIONTYPE portdef;

	LOG_DEBUG("Setting up OMX JPEG input port ...");

	if (component_get_portdef(&omx->encoder, &portdef, _INPUT_PORT) < 0) {
		LOG_ERROR("... first");
		return -1;
	}

	portdef.format.image.nFrameWidth = dev->run->width;
	portdef.format.image.nFrameHeight = dev->run->height;
	portdef.format.image.nStride = 0;
#	define ALIGN_HEIGHT(_x, _y) (((_x) + ((_y) - 1)) & ~((_y) - 1))
	portdef.format.image.nSliceHeight = ALIGN_HEIGHT(dev->run->height, 16);
#	undef ALIGN_HEIGHT
	portdef.format.image.bFlagErrorConcealment = OMX_FALSE;
	portdef.format.image.eCompressionFormat = OMX_IMAGE_CodingUnused;
	portdef.nBufferSize = dev->run->max_raw_image_size;

#	define MAP_FORMAT(_v4l2_format, _omx_format) \
		case _v4l2_format: { portdef.format.image.eColorFormat = _omx_format; break; }

	switch (dev->run->format) {
		// https://www.fourcc.org/yuv.php
		// Also see comments inside OMX_IVCommon.h
		MAP_FORMAT(V4L2_PIX_FMT_YUYV, OMX_COLOR_FormatYCbYCr);
		MAP_FORMAT(V4L2_PIX_FMT_UYVY, OMX_COLOR_FormatCbYCrY);
		MAP_FORMAT(V4L2_PIX_FMT_RGB565, OMX_COLOR_Format16bitRGB565);
		MAP_FORMAT(V4L2_PIX_FMT_RGB24, OMX_COLOR_Format24bitRGB888);
		// TODO: найти устройство с RGB565 и протестить его.
		// FIXME: RGB24 не работает нормально, нижняя половина экрана зеленая.
		// FIXME: Китайский EasyCap тоже не работает, мусор на экране.
		// Вероятно обе проблемы вызваны некорректной реализацией OMX на пае.
		default: assert(0 && "Unsupported input format for OMX JPEG encoder");
	}

#	undef MAP_FORMAT

	if (component_set_portdef(&omx->encoder, &portdef) < 0) {
		return -1;
	}

	if (component_get_portdef(&omx->encoder, &portdef, _INPUT_PORT) < 0) {
		LOG_ERROR("... second");
		return -1;
	}

	if (component_enable_port(&omx->encoder, _INPUT_PORT) < 0) {
		return -1;
	}
	omx->i_input_port_enabled = true;

	if ((error = OMX_AllocateBuffer(omx->encoder, &omx->input_buffer, _INPUT_PORT, NULL, portdef.nBufferSize)) != OMX_ErrorNone) {
		LOG_OMX_ERROR(error, "Can't allocate OMX JPEG input buffer");
		return -1;
	}
	return 0;
}

static int _omx_setup_output(struct omx_encoder_t *omx, unsigned quality) {
	OMX_ERRORTYPE error;
	OMX_PARAM_PORTDEFINITIONTYPE portdef;

	LOG_DEBUG("Setting up OMX JPEG output port ...");

	if (component_get_portdef(&omx->encoder, &portdef, _OUTPUT_PORT) < 0) {
		LOG_ERROR("... first");
		return -1;
	}

	portdef.format.image.bFlagErrorConcealment = OMX_FALSE;
	portdef.format.image.eCompressionFormat = OMX_IMAGE_CodingJPEG;
	portdef.format.image.eColorFormat = OMX_COLOR_FormatYCbYCr;

	if (component_set_portdef(&omx->encoder, &portdef) < 0) {
		return -1;
	}

	if (component_get_portdef(&omx->encoder, &portdef, _OUTPUT_PORT) < 0) {
		LOG_ERROR("... second");
		return -1;
	}

	{
		OMX_CONFIG_BOOLEANTYPE exif;

		OMX_INIT_STRUCTURE(exif);
		exif.bEnabled = OMX_FALSE;

		if ((error = OMX_SetParameter(omx->encoder, OMX_IndexParamBrcmDisableEXIF, &exif)) != OMX_ErrorNone) {
			LOG_OMX_ERROR(error, "Can't disable EXIF on OMX JPEG");
			return -1;
		}
	}

	{
		OMX_PARAM_IJGSCALINGTYPE ijg;

		OMX_INIT_STRUCTURE(ijg);
		ijg.nPortIndex = _OUTPUT_PORT;
		ijg.bEnabled = OMX_TRUE;

		if ((error = OMX_SetParameter(omx->encoder, OMX_IndexParamBrcmEnableIJGTableScaling, &ijg)) != OMX_ErrorNone) {
			LOG_OMX_ERROR(error, "Can't set OMX JPEG IJG settings");
			return -1;
		}
	}

	{
		OMX_IMAGE_PARAM_QFACTORTYPE qfactor;

		OMX_INIT_STRUCTURE(qfactor);
		qfactor.nPortIndex = _OUTPUT_PORT;
		qfactor.nQFactor = quality;

		if ((error = OMX_SetParameter(omx->encoder, OMX_IndexParamQFactor, &qfactor)) != OMX_ErrorNone) {
			LOG_OMX_ERROR(error, "Can't set OMX JPEG quality");
			return -1;
		}
	}

	if (component_enable_port(&omx->encoder, _OUTPUT_PORT) < 0) {
		return -1;
	}
	omx->i_output_port_enabled = true;

	if ((error = OMX_AllocateBuffer(omx->encoder, &omx->output_buffer, _OUTPUT_PORT, NULL, portdef.nBufferSize)) != OMX_ErrorNone) {
		LOG_OMX_ERROR(error, "Can't allocate OMX JPEG output buffer");
		return -1;
	}
	return 0;
}

static int _omx_encoder_clear_ports(struct omx_encoder_t *omx) {
	OMX_ERRORTYPE error;
	int retcode = 0;

	if (omx->i_output_port_enabled) {
		retcode -= component_disable_port(&omx->encoder, _OUTPUT_PORT);
		omx->i_output_port_enabled = false;
	}
	if (omx->i_input_port_enabled) {
		retcode -= component_disable_port(&omx->encoder, _INPUT_PORT);
		omx->i_input_port_enabled = false;
	}

	if (omx->input_buffer) {
		if ((error = OMX_FreeBuffer(omx->encoder, _INPUT_PORT, omx->input_buffer)) != OMX_ErrorNone) {
			LOG_OMX_ERROR(error, "Can't free OMX JPEG input buffer");
			// retcode -= 1;
		}
		omx->input_buffer = NULL;
	}
	if (omx->output_buffer) {
		if ((error = OMX_FreeBuffer(omx->encoder, _OUTPUT_PORT, omx->output_buffer)) != OMX_ErrorNone) {
			LOG_OMX_ERROR(error, "Can't free OMX JPEG output buffer");
			// retcode -= 1;
		}
		omx->output_buffer = NULL;
	}
	return retcode;
}

static OMX_ERRORTYPE _omx_event_handler(
	UNUSED OMX_HANDLETYPE encoder,
	OMX_PTR v_omx, OMX_EVENTTYPE event, OMX_U32 data1,
	UNUSED OMX_U32 data2, UNUSED OMX_PTR event_data) {

	// OMX calls this handler for all the events it emits

	struct omx_encoder_t *omx = (struct omx_encoder_t *)v_omx;

	if (event == OMX_EventError) {
		LOG_OMX_ERROR((OMX_ERRORTYPE)data1, "OMX error event received");
		omx->failed = true;
		vcos_semaphore_post(&omx->handler_lock);
	}
	return OMX_ErrorNone;
}

static OMX_ERRORTYPE _omx_input_required_handler(
	UNUSED OMX_HANDLETYPE encoder,
	OMX_PTR v_omx, UNUSED OMX_BUFFERHEADERTYPE *buffer) {

	// Called by OMX when the encoder component requires
	// the input buffer to be filled with RAW image data

	struct omx_encoder_t *omx = (struct omx_encoder_t *)v_omx;

	omx->input_required = true;
	vcos_semaphore_post(&omx->handler_lock);
	return OMX_ErrorNone;
}

static OMX_ERRORTYPE _omx_output_available_handler(
	UNUSED OMX_HANDLETYPE encoder,
	OMX_PTR v_omx, UNUSED OMX_BUFFERHEADERTYPE *buffer) {

	// Called by OMX when the encoder component has filled
	// the output buffer with JPEG data

	struct omx_encoder_t *omx = (struct omx_encoder_t *)v_omx;

	omx->output_available = true;
	vcos_semaphore_post(&omx->handler_lock);
	return OMX_ErrorNone;
}
