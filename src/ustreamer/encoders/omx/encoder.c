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


static const OMX_U32 _INPUT_PORT = 340;
static const OMX_U32 _OUTPUT_PORT = 341;


static int _vcos_semwait(VCOS_SEMAPHORE_T *sem);
static int _omx_init_component(omx_encoder_s *omx);
static int _omx_init_disable_ports(omx_encoder_s *omx);
static int _omx_setup_input(omx_encoder_s *omx, device_s *dev);
static int _omx_setup_output(omx_encoder_s *omx, unsigned quality);
static int _omx_encoder_clear_ports(omx_encoder_s *omx);

static OMX_ERRORTYPE _omx_event_handler(
	UNUSED OMX_HANDLETYPE comp,
	OMX_PTR v_omx, OMX_EVENTTYPE event, OMX_U32 data1,
	UNUSED OMX_U32 data2, UNUSED OMX_PTR event_data);

static OMX_ERRORTYPE _omx_input_required_handler(
	UNUSED OMX_HANDLETYPE comp,
	OMX_PTR v_omx, UNUSED OMX_BUFFERHEADERTYPE *buffer);

static OMX_ERRORTYPE _omx_output_available_handler(
	UNUSED OMX_HANDLETYPE comp,
	OMX_PTR v_omx, UNUSED OMX_BUFFERHEADERTYPE *buffer);


omx_encoder_s *omx_encoder_init(void) {
	// Some theory:
	//   - http://www.fourcc.org/yuv.php
	//   - https://kwasi-ich.de/blog/2017/11/26/omx/
	//   - https://github.com/hopkinskong/rpi-omx-jpeg-encode/blob/master/jpeg_bench.cpp
	//   - https://github.com/kwasmich/OMXPlayground/blob/master/omxJPEGEnc.c
	//   - https://github.com/gagle/raspberrypi-openmax-jpeg/blob/master/jpeg.c
	//   - https://www.raspberrypi.org/forums/viewtopic.php?t=154790
	//   - https://bitbucket.org/bensch128/omxjpegencode/src/master/jpeg_encoder.cpp
	//   - http://home.nouwen.name/RaspberryPi/documentation/ilcomponents/image_encode.html

	LOG_INFO("Initializing OMX encoder ...");

	omx_encoder_s *omx;
	A_CALLOC(omx, 1);

	if (vcos_semaphore_create(&omx->handler_sem, "handler_sem", 0) != VCOS_SUCCESS) {
		LOG_ERROR("Can't create VCOS semaphore");
		goto error;
	}
	omx->i_handler_sem = true;

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

void omx_encoder_destroy(omx_encoder_s *omx) {
	LOG_INFO("Destroying OMX encoder ...");

	omx_component_set_state(&omx->comp, OMX_StateIdle);
	_omx_encoder_clear_ports(omx);
	omx_component_set_state(&omx->comp, OMX_StateLoaded);

	if (omx->i_handler_sem) {
		vcos_semaphore_delete(&omx->handler_sem);
	}

	if (omx->i_encoder) {
		OMX_ERRORTYPE error;
		if ((error = OMX_FreeHandle(omx->comp)) != OMX_ErrorNone) {
			LOG_ERROR_OMX(error, "Can't free OMX.broadcom.image_encode");
		}
	}

	free(omx);
}

int omx_encoder_prepare(omx_encoder_s *omx, device_s *dev, unsigned quality) {
	if (omx_component_set_state(&omx->comp, OMX_StateIdle) < 0) {
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
	if (omx_component_set_state(&omx->comp, OMX_StateExecuting) < 0) {
		return -1;
	}
	return 0;
}

int omx_encoder_compress(omx_encoder_s *omx, const frame_s *src, frame_s *dest) {
#	define IN(_next)	omx->input_buffer->_next
#	define OUT(_next)	omx->output_buffer->_next

	OMX_ERRORTYPE error;

	if ((error = OMX_FillThisBuffer(omx->comp, omx->output_buffer)) != OMX_ErrorNone) {
		LOG_ERROR_OMX(error, "Failed to request filling of the output buffer on encoder");
		return -1;
	}

	dest->used = 0;
	omx->output_available = false;
	omx->input_required = true;

	size_t slice_size = (IN(nAllocLen) < src->used ? IN(nAllocLen) : src->used);
	size_t pos = 0;

	while (true) {
		if (omx->failed) {
			return -1;
		}

		if (omx->output_available) {
			omx->output_available = false;

			frame_append_data(dest, OUT(pBuffer) + OUT(nOffset), OUT(nFilledLen));

			if (OUT(nFlags) & OMX_BUFFERFLAG_ENDOFFRAME) {
				OUT(nFlags) = 0;
				break;
			}

			if ((error = OMX_FillThisBuffer(omx->comp, omx->output_buffer)) != OMX_ErrorNone) {
				LOG_ERROR_OMX(error, "Failed to request filling of the output buffer on encoder");
				return -1;
			}
		}

		if (omx->input_required) {
			omx->input_required = false;

			if (pos == src->used) {
				continue;
			}

			memcpy(IN(pBuffer), src->data + pos, slice_size);
			IN(nOffset) = 0;
			IN(nFilledLen) = slice_size;

			pos += slice_size;

			if (pos + slice_size > src->used) {
				slice_size = src->used - pos;
			}

			if ((error = OMX_EmptyThisBuffer(omx->comp, omx->input_buffer)) != OMX_ErrorNone) {
				LOG_ERROR_OMX(error, "Failed to request emptying of the input buffer on encoder");
				return -1;
			}
		}

		if (_vcos_semwait(&omx->handler_sem) != 0) {
			return -1;
		}
	}

#	undef OUT
#	undef IN
	return 0;
}

static int _vcos_semwait(VCOS_SEMAPHORE_T *sem) {
	// vcos_semaphore_wait() can wait infinite
	// vcos_semaphore_wait_timeout() is broken by design:
	//   - https://github.com/pikvm/ustreamer/issues/56
	//   - https://github.com/raspberrypi/userland/issues/658
	// CFG_OMX_SEMWAIT_TIMEOUT is ugly busyloop
	// Три стула.

#	ifdef CFG_OMX_SEMWAIT_TIMEOUT
	long double deadline_ts = get_now_monotonic() + (long double)CFG_OMX_SEMWAIT_TIMEOUT; // Seconds
	VCOS_STATUS_T sem_status;

	while (true) {
		sem_status = vcos_semaphore_trywait(sem);
		if (sem_status == VCOS_SUCCESS) {
			return 0;
		} else if (sem_status != VCOS_EAGAIN || get_now_monotonic() > deadline_ts) {
			goto error;
		}
		usleep(1000);
	}

	error:
		switch (sem_status) {
			case VCOS_EAGAIN: LOG_ERROR("Can't wait VCOS semaphore: EAGAIN (timeout)"); break;
			case VCOS_EINVAL: LOG_ERROR("Can't wait VCOS semaphore: EINVAL"); break;
			default: LOG_ERROR("Can't wait VCOS semaphore: %d", sem_status); break;
		}
		return -1;
#	else
	return (vcos_semaphore_wait(sem) == VCOS_SUCCESS ? 0 : -1);
#	endif
}

static int _omx_init_component(omx_encoder_s *omx) {
	OMX_ERRORTYPE error;

	OMX_CALLBACKTYPE callbacks;
	MEMSET_ZERO(callbacks);
	callbacks.EventHandler = _omx_event_handler;
	callbacks.EmptyBufferDone = _omx_input_required_handler;
	callbacks.FillBufferDone = _omx_output_available_handler;

	LOG_DEBUG("Initializing OMX.broadcom.image_encode ...");
	if ((error = OMX_GetHandle(&omx->comp, "OMX.broadcom.image_encode", omx, &callbacks)) != OMX_ErrorNone) {
		LOG_ERROR_OMX(error, "Can't initialize OMX.broadcom.image_encode");
		return -1;
	}
	omx->i_encoder = true;
	return 0;
}

static int _omx_init_disable_ports(omx_encoder_s *omx) {
	OMX_ERRORTYPE error;
	OMX_INDEXTYPE types[] = {
		OMX_IndexParamAudioInit, OMX_IndexParamVideoInit,
		OMX_IndexParamImageInit, OMX_IndexParamOtherInit,
	};
	OMX_PORT_PARAM_TYPE ports;

	OMX_INIT_STRUCTURE(ports);
	if ((error = OMX_GetParameter(omx->comp, OMX_IndexParamImageInit, &ports)) != OMX_ErrorNone) {
		LOG_ERROR_OMX(error, "Can't OMX_GetParameter(OMX_IndexParamImageInit)");
		return -1;
	}

	for (unsigned index = 0; index < 4; ++index) {
		if ((error = OMX_GetParameter(omx->comp, types[index], &ports)) != OMX_ErrorNone) {
			LOG_ERROR_OMX(error, "Can't OMX_GetParameter(types[%u])", index);
			return -1;
		}
		for (OMX_U32 port = ports.nStartPortNumber; port < ports.nStartPortNumber + ports.nPorts; ++port) {
			if (omx_component_disable_port(&omx->comp, port) < 0) {
				return -1;
			}
		}
	}
	return 0;
}

static int _omx_setup_input(omx_encoder_s *omx, device_s *dev) {
	LOG_DEBUG("Setting up OMX JPEG input port ...");

	OMX_ERRORTYPE error;
	OMX_PARAM_PORTDEFINITIONTYPE portdef;

	if (omx_component_get_portdef(&omx->comp, &portdef, _INPUT_PORT) < 0) {
		LOG_ERROR("... first");
		return -1;
	}

#	define IFMT(_next) portdef.format.image._next
	IFMT(nFrameWidth) = dev->run->width;
	IFMT(nFrameHeight) = dev->run->height;
	IFMT(nStride) = 0;
	IFMT(nSliceHeight) = align_size(dev->run->height, 16);
	IFMT(bFlagErrorConcealment) = OMX_FALSE;
	IFMT(eCompressionFormat) = OMX_IMAGE_CodingUnused;
	portdef.nBufferSize = ((dev->run->width * dev->run->height) << 1) * 2;
	switch (dev->run->format) {
		// https://www.fourcc.org/yuv.php
		// Also see comments inside OMX_IVCommon.h
		case V4L2_PIX_FMT_YUYV:		IFMT(eColorFormat) = OMX_COLOR_FormatYCbYCr; break;
		case V4L2_PIX_FMT_UYVY:		IFMT(eColorFormat) = OMX_COLOR_FormatCbYCrY; break;
		case V4L2_PIX_FMT_RGB565:	IFMT(eColorFormat) = OMX_COLOR_Format16bitRGB565; break;
		case V4L2_PIX_FMT_RGB24:	IFMT(eColorFormat) = OMX_COLOR_Format24bitRGB888; break;
		// TODO: найти устройство с RGB565 и протестить его.
		// FIXME: RGB24 не работает нормально, нижняя половина экрана зеленая.
		// FIXME: Китайский EasyCap тоже не работает, мусор на экране.
		// Вероятно обе проблемы вызваны некорректной реализацией OMX на пае.
		default: assert(0 && "Unsupported input format for OMX JPEG encoder");
	}
#	undef IFMT

	if (omx_component_set_portdef(&omx->comp, &portdef) < 0) {
		return -1;
	}

	if (omx_component_get_portdef(&omx->comp, &portdef, _INPUT_PORT) < 0) {
		LOG_ERROR("... second");
		return -1;
	}

	if (omx_component_enable_port(&omx->comp, _INPUT_PORT) < 0) {
		return -1;
	}
	omx->i_input_port_enabled = true;

	if ((error = OMX_AllocateBuffer(omx->comp, &omx->input_buffer, _INPUT_PORT, NULL, portdef.nBufferSize)) != OMX_ErrorNone) {
		LOG_ERROR_OMX(error, "Can't allocate OMX JPEG input buffer");
		return -1;
	}
	return 0;
}

static int _omx_setup_output(omx_encoder_s *omx, unsigned quality) {
	LOG_DEBUG("Setting up OMX JPEG output port ...");

	OMX_ERRORTYPE error;
	OMX_PARAM_PORTDEFINITIONTYPE portdef;

	if (omx_component_get_portdef(&omx->comp, &portdef, _OUTPUT_PORT) < 0) {
		LOG_ERROR("... first");
		return -1;
	}

#	define OFMT(_next) portdef.format.image._next
	OFMT(bFlagErrorConcealment) = OMX_FALSE;
	OFMT(eCompressionFormat) = OMX_IMAGE_CodingJPEG;
	OFMT(eColorFormat) = OMX_COLOR_FormatYCbYCr;
#	undef OFMT

	if (omx_component_set_portdef(&omx->comp, &portdef) < 0) {
		return -1;
	}

	if (omx_component_get_portdef(&omx->comp, &portdef, _OUTPUT_PORT) < 0) {
		LOG_ERROR("... second");
		return -1;
	}

	{
		OMX_CONFIG_BOOLEANTYPE exif;

		OMX_INIT_STRUCTURE(exif);
		exif.bEnabled = OMX_FALSE;

		if ((error = OMX_SetParameter(omx->comp, OMX_IndexParamBrcmDisableEXIF, &exif)) != OMX_ErrorNone) {
			LOG_ERROR_OMX(error, "Can't disable EXIF on OMX JPEG");
			return -1;
		}
	}

	{
		OMX_PARAM_IJGSCALINGTYPE ijg;

		OMX_INIT_STRUCTURE(ijg);
		ijg.nPortIndex = _OUTPUT_PORT;
		ijg.bEnabled = OMX_TRUE;

		if ((error = OMX_SetParameter(omx->comp, OMX_IndexParamBrcmEnableIJGTableScaling, &ijg)) != OMX_ErrorNone) {
			LOG_ERROR_OMX(error, "Can't set OMX JPEG IJG settings");
			return -1;
		}
	}

	{
		OMX_IMAGE_PARAM_QFACTORTYPE qfactor;

		OMX_INIT_STRUCTURE(qfactor);
		qfactor.nPortIndex = _OUTPUT_PORT;
		qfactor.nQFactor = quality;

		if ((error = OMX_SetParameter(omx->comp, OMX_IndexParamQFactor, &qfactor)) != OMX_ErrorNone) {
			LOG_ERROR_OMX(error, "Can't set OMX JPEG quality");
			return -1;
		}
	}

	if (omx_component_enable_port(&omx->comp, _OUTPUT_PORT) < 0) {
		return -1;
	}
	omx->i_output_port_enabled = true;

	if ((error = OMX_AllocateBuffer(omx->comp, &omx->output_buffer, _OUTPUT_PORT, NULL, portdef.nBufferSize)) != OMX_ErrorNone) {
		LOG_ERROR_OMX(error, "Can't allocate OMX JPEG output buffer");
		return -1;
	}
	return 0;
}

static int _omx_encoder_clear_ports(omx_encoder_s *omx) {
	OMX_ERRORTYPE error;
	int retval = 0;

	if (omx->i_output_port_enabled) {
		retval -= omx_component_disable_port(&omx->comp, _OUTPUT_PORT);
		omx->i_output_port_enabled = false;
	}
	if (omx->i_input_port_enabled) {
		retval -= omx_component_disable_port(&omx->comp, _INPUT_PORT);
		omx->i_input_port_enabled = false;
	}

	if (omx->input_buffer) {
		if ((error = OMX_FreeBuffer(omx->comp, _INPUT_PORT, omx->input_buffer)) != OMX_ErrorNone) {
			LOG_ERROR_OMX(error, "Can't free OMX JPEG input buffer");
			// retval -= 1;
		}
		omx->input_buffer = NULL;
	}
	if (omx->output_buffer) {
		if ((error = OMX_FreeBuffer(omx->comp, _OUTPUT_PORT, omx->output_buffer)) != OMX_ErrorNone) {
			LOG_ERROR_OMX(error, "Can't free OMX JPEG output buffer");
			// retval -= 1;
		}
		omx->output_buffer = NULL;
	}
	return retval;
}

static OMX_ERRORTYPE _omx_event_handler(
	UNUSED OMX_HANDLETYPE comp,
	OMX_PTR v_omx, OMX_EVENTTYPE event, OMX_U32 data1,
	UNUSED OMX_U32 data2, UNUSED OMX_PTR event_data) {

	// OMX calls this handler for all the events it emits

	omx_encoder_s *omx = (omx_encoder_s *)v_omx;

	if (event == OMX_EventError) {
		LOG_ERROR_OMX((OMX_ERRORTYPE)data1, "OMX error event received");
		omx->failed = true;
		assert(vcos_semaphore_post(&omx->handler_sem) == VCOS_SUCCESS);
	}
	return OMX_ErrorNone;
}

static OMX_ERRORTYPE _omx_input_required_handler(
	UNUSED OMX_HANDLETYPE comp,
	OMX_PTR v_omx, UNUSED OMX_BUFFERHEADERTYPE *buffer) {

	// Called by OMX when the encoder component requires
	// the input buffer to be filled with RAW image data

	omx_encoder_s *omx = (omx_encoder_s *)v_omx;

	omx->input_required = true;
	assert(vcos_semaphore_post(&omx->handler_sem) == VCOS_SUCCESS);
	return OMX_ErrorNone;
}

static OMX_ERRORTYPE _omx_output_available_handler(
	UNUSED OMX_HANDLETYPE comp,
	OMX_PTR v_omx, UNUSED OMX_BUFFERHEADERTYPE *buffer) {

	// Called by OMX when the encoder component has filled
	// the output buffer with JPEG data

	omx_encoder_s *omx = (omx_encoder_s *)v_omx;

	omx->output_available = true;
	assert(vcos_semaphore_post(&omx->handler_sem) == VCOS_SUCCESS);
	return OMX_ErrorNone;
}
