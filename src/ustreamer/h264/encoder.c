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


#include "encoder.h"


static void _h264_encoder_cleanup(h264_encoder_s *enc);
static int _h264_encoder_compress_raw(h264_encoder_s *enc, const frame_s *src, int src_vcsm_handle, frame_s *dest, bool force_key);

static void _mmal_callback(MMAL_WRAPPER_T *wrapper);
static const char *_mmal_error_to_string(MMAL_STATUS_T error);


#define LOG_ERROR_MMAL(_error, _msg, ...) { \
		LOG_ERROR(_msg ": %s", ##__VA_ARGS__, _mmal_error_to_string(_error)); \
	}


h264_encoder_s *h264_encoder_init(unsigned bitrate, unsigned gop, unsigned fps) {
	LOG_INFO("H264: Initializing MMAL encoder ...");
	LOG_INFO("H264: Using bitrate: %u Kbps", bitrate);
	LOG_INFO("H264: Using GOP: %u", gop);

	h264_encoder_s *enc;
	A_CALLOC(enc, 1);
	enc->bitrate = bitrate; // Kbps
	enc->gop = gop; // Interval between keyframes
	enc->fps = fps;

	enc->last_online = -1;

	if (vcos_semaphore_create(&enc->handler_sem, "h264_handler_sem", 0) != VCOS_SUCCESS) {
		LOG_PERROR("H264: Can't create VCOS semaphore");
		goto error;
	}
	enc->i_handler_sem = true;

	MMAL_STATUS_T error = mmal_wrapper_create(&enc->wrapper, MMAL_COMPONENT_DEFAULT_VIDEO_ENCODER);
	if (error != MMAL_SUCCESS) {
		LOG_ERROR_MMAL(error, "H264: Can't create MMAL wrapper");
		enc->wrapper = NULL;
		goto error;
	}
	enc->wrapper->user_data = (void *)enc;
	enc->wrapper->callback = _mmal_callback;

	return enc;

	error:
		h264_encoder_destroy(enc);
		return NULL;
}

void h264_encoder_destroy(h264_encoder_s *enc) {
	LOG_INFO("H264: Destroying MMAL encoder ...");

	_h264_encoder_cleanup(enc);

	if (enc->wrapper) {
		MMAL_STATUS_T error = mmal_wrapper_destroy(enc->wrapper);
		if (error != MMAL_SUCCESS) {
			LOG_ERROR_MMAL(error, "H264: Can't destroy MMAL encoder");
		}
	}

	if (enc->i_handler_sem) {
		vcos_semaphore_delete(&enc->handler_sem);
	}

	free(enc);
}

bool h264_encoder_is_prepared_for(h264_encoder_s *enc, const frame_s *frame, bool zero_copy) {
#	define EQ(_field) (enc->_field == frame->_field)
	return (EQ(width) && EQ(height) && EQ(format) && EQ(stride) && (enc->zero_copy == zero_copy));
#	undef EQ
}

int h264_encoder_prepare(h264_encoder_s *enc, const frame_s *frame, bool zero_copy) {
	LOG_INFO("H264: Configuring MMAL encoder: zero_copy=%d ...", zero_copy);

	_h264_encoder_cleanup(enc);

	enc->width = frame->width;
	enc->height = frame->height;
	enc->format = frame->format;
	enc->stride = frame->stride;
	enc->zero_copy = zero_copy;

	if (align_size(frame->width, 32) != frame->width && frame_get_padding(frame) == 0) {
		LOG_ERROR("H264: MMAL encoder can't handle unaligned width");
		goto error;
	}

	MMAL_STATUS_T error;

#	define PREPARE_PORT(_id) { \
			enc->_id##_port = enc->wrapper->_id[0]; \
			if (enc->_id##_port->is_enabled) { \
				if ((error = mmal_wrapper_port_disable(enc->_id##_port)) != MMAL_SUCCESS) { \
					LOG_ERROR_MMAL(error, "H264: Can't disable MMAL %s port while configuring", #_id); \
					goto error; \
				} \
			} \
		}

#	define COMMIT_PORT(_id) { \
			if ((error = mmal_port_format_commit(enc->_id##_port)) != MMAL_SUCCESS) { \
				LOG_ERROR_MMAL(error, "H264: Can't commit MMAL %s port", #_id); \
				goto error; \
			} \
		}

#	define SET_PORT_PARAM(_id, _type, _key, _value) { \
			if ((error = mmal_port_parameter_set_##_type(enc->_id##_port, MMAL_PARAMETER_##_key, _value)) != MMAL_SUCCESS) { \
				LOG_ERROR_MMAL(error, "H264: Can't set %s for the %s port", #_key, #_id); \
				goto error; \
			} \
		}

#	define ENABLE_PORT(_id) { \
			if ((error = mmal_wrapper_port_enable(enc->_id##_port, MMAL_WRAPPER_FLAG_PAYLOAD_ALLOCATE)) != MMAL_SUCCESS) { \
				LOG_ERROR_MMAL(error, "H264: Can't enable MMAL %s port", #_id); \
				goto error; \
			} \
		}

	{
		PREPARE_PORT(input);

#		define IFMT(_next) enc->input_port->format->_next
		IFMT(type) = MMAL_ES_TYPE_VIDEO;
		switch (frame->format) {
			case V4L2_PIX_FMT_YUYV:		IFMT(encoding) = MMAL_ENCODING_YUYV; break;
			case V4L2_PIX_FMT_UYVY:		IFMT(encoding) = MMAL_ENCODING_UYVY; break;
			case V4L2_PIX_FMT_RGB565:	IFMT(encoding) = MMAL_ENCODING_RGB16; break;
			case V4L2_PIX_FMT_RGB24:	IFMT(encoding) = MMAL_ENCODING_RGB24; break;
			default: assert(0 && "Unsupported pixelformat");
		}
		IFMT(es->video.width) = align_size(frame->width, 32);
		IFMT(es->video.height) = align_size(frame->height, 16);
		IFMT(es->video.crop.x) = 0;
		IFMT(es->video.crop.y) = 0;
		IFMT(es->video.crop.width) = frame->width;
		IFMT(es->video.crop.height) = frame->height;
		IFMT(flags) = MMAL_ES_FORMAT_FLAG_FRAMED;
		enc->input_port->buffer_size = 1000 * 1000;
		enc->input_port->buffer_num = enc->input_port->buffer_num_recommended * 4;
#		undef IFMT

		COMMIT_PORT(input);
		SET_PORT_PARAM(input, boolean, ZERO_COPY, zero_copy);
	}

	{
		PREPARE_PORT(output);

#		define OFMT(_next) enc->output_port->format->_next
		OFMT(type) = MMAL_ES_TYPE_VIDEO;
		OFMT(encoding) = MMAL_ENCODING_H264;
		OFMT(encoding_variant) = MMAL_ENCODING_VARIANT_H264_DEFAULT;
		OFMT(bitrate) = enc->bitrate * 1000;
		OFMT(es->video.frame_rate.num) = enc->fps;
		OFMT(es->video.frame_rate.den) = 1;
		enc->output_port->buffer_size = enc->output_port->buffer_size_recommended * 4;
		enc->output_port->buffer_num = enc->output_port->buffer_num_recommended;
#		undef OFMT

		COMMIT_PORT(output);
		{
			MMAL_PARAMETER_VIDEO_PROFILE_T profile;
			MEMSET_ZERO(profile);
			profile.hdr.id = MMAL_PARAMETER_PROFILE;
			profile.hdr.size = sizeof(profile);
			// http://blog.mediacoderhq.com/h264-profiles-and-levels
			profile.profile[0].profile = MMAL_VIDEO_PROFILE_H264_CONSTRAINED_BASELINE;
			profile.profile[0].level = MMAL_VIDEO_LEVEL_H264_4; // Supports 1080p
			if ((error = mmal_port_parameter_set(enc->output_port, &profile.hdr)) != MMAL_SUCCESS) {
				LOG_ERROR_MMAL(error, "H264: Can't set MMAL_PARAMETER_PROFILE for the output port");
				goto error;
			}
		}

		SET_PORT_PARAM(output, boolean,	ZERO_COPY,					MMAL_TRUE);
		SET_PORT_PARAM(output, uint32,	INTRAPERIOD,				enc->gop);
		SET_PORT_PARAM(output, uint32,	NALUNITFORMAT,				MMAL_VIDEO_NALUNITFORMAT_STARTCODES);
		SET_PORT_PARAM(output, boolean,	MINIMISE_FRAGMENTATION,		MMAL_TRUE);
		SET_PORT_PARAM(output, uint32,	MB_ROWS_PER_SLICE,			0);
		SET_PORT_PARAM(output, boolean,	VIDEO_IMMUTABLE_INPUT,		MMAL_TRUE);
		SET_PORT_PARAM(output, boolean,	VIDEO_DROPPABLE_PFRAMES,	MMAL_FALSE);
		SET_PORT_PARAM(output, boolean,	VIDEO_ENCODE_INLINE_HEADER,	MMAL_TRUE); // SPS/PPS: https://github.com/raspberrypi/userland/issues/443
		SET_PORT_PARAM(output, uint32,	VIDEO_BIT_RATE,				enc->bitrate * 1000);
		SET_PORT_PARAM(output, uint32,	VIDEO_ENCODE_PEAK_RATE,		enc->bitrate * 1000);
		SET_PORT_PARAM(output, uint32,	VIDEO_ENCODE_MIN_QUANT,				16);
		SET_PORT_PARAM(output, uint32,	VIDEO_ENCODE_MAX_QUANT,				34);
		// Этот параметр с этим значением фризит кодирование изображения из черно-белой консоли
		// SET_PORT_PARAM(output, uint32,	VIDEO_ENCODE_FRAME_LIMIT_BITS,		1000000);
		SET_PORT_PARAM(output, uint32,	VIDEO_ENCODE_H264_AU_DELIMITERS,	MMAL_FALSE);
	}

	ENABLE_PORT(input);
	ENABLE_PORT(output);

	enc->ready = true;
	return 0;

	error:
		_h264_encoder_cleanup(enc);
		LOG_ERROR("H264: Encoder destroyed due an error (prepare)");
		return -1;

#	undef ENABLE_PORT
#	undef SET_PORT_PARAM
#	undef COMMIT_PORT
#	undef PREPARE_PORT
}

static void _h264_encoder_cleanup(h264_encoder_s *enc) {
	MMAL_STATUS_T error;

#	define DISABLE_PORT(_id) { \
			if (enc->_id##_port) { \
				if ((error = mmal_wrapper_port_disable(enc->_id##_port)) != MMAL_SUCCESS) { \
					LOG_ERROR_MMAL(error, "H264: Can't disable MMAL %s port", #_id); \
				} \
				enc->_id##_port = NULL; \
			} \
		}

	DISABLE_PORT(input);
	DISABLE_PORT(output);

#	undef DISABLE_PORT

	if (enc->wrapper) {
		enc->wrapper->status = MMAL_SUCCESS; // Это реально надо?
	}

	enc->last_online = -1;
	enc->ready = false;
}

int h264_encoder_compress(h264_encoder_s *enc, const frame_s *src, int src_vcsm_handle, frame_s *dest, bool force_key) {
	assert(enc->ready);
	assert(src->used > 0);
	assert(enc->width == src->width);
	assert(enc->height == src->height);
	assert(enc->format == src->format);
	assert(enc->stride == src->stride);

	frame_copy_meta(src, dest);
	dest->encode_begin_ts = get_now_monotonic();
	dest->format = V4L2_PIX_FMT_H264;
	dest->stride = 0;

	force_key = (force_key || enc->last_online != src->online);

	if (_h264_encoder_compress_raw(enc, src, src_vcsm_handle, dest, force_key) < 0) {
		_h264_encoder_cleanup(enc);
		LOG_ERROR("H264: Encoder destroyed due an error (compress)");
		return -1;
	}

	dest->encode_end_ts = get_now_monotonic();
	LOG_VERBOSE("H264: Compressed new frame: size=%zu, time=%0.3Lf, force_key=%d",
		dest->used, dest->encode_end_ts - dest->encode_begin_ts, force_key);

	enc->last_online = src->online;
	return 0;
}

static int _h264_encoder_compress_raw(h264_encoder_s *enc, const frame_s *src, int src_vcsm_handle, frame_s *dest, bool force_key) {
	LOG_DEBUG("H264: Compressing new frame; force_key=%d ...", force_key);

	MMAL_STATUS_T error;

	if (force_key) {
		if ((error = mmal_port_parameter_set_boolean(
			enc->output_port,
			MMAL_PARAMETER_VIDEO_REQUEST_I_FRAME,
			MMAL_TRUE
		)) != MMAL_SUCCESS) {
			LOG_ERROR_MMAL(error, "H264: Can't request keyframe");
			return -1;
		}
	}

	MMAL_BUFFER_HEADER_T *out = NULL;
	MMAL_BUFFER_HEADER_T *in = NULL;
	bool eos = false;
	bool sent = false;

	dest->used = 0;

	while (!eos) {
		out = NULL;
		while (mmal_wrapper_buffer_get_empty(enc->output_port, &out, 0) == MMAL_SUCCESS) {
			if ((error = mmal_port_send_buffer(enc->output_port, out)) != MMAL_SUCCESS) {
				LOG_ERROR_MMAL(error, "H264: Can't send MMAL output buffer");
				return -1;
			}
		}

		in = NULL;
		if (!sent && mmal_wrapper_buffer_get_empty(enc->input_port, &in, 0) == MMAL_SUCCESS) {
			if (enc->zero_copy && src_vcsm_handle > 0) {
				in->data = (uint8_t *)vcsm_vc_hdl_from_hdl(src_vcsm_handle);
			} else {
				in->data = src->data;
			}
			in->alloc_size = src->used;
			in->length = src->used;
			in->offset = 0;
			in->flags = MMAL_BUFFER_HEADER_FLAG_EOS;
			if ((error = mmal_port_send_buffer(enc->input_port, in)) != MMAL_SUCCESS) {
				LOG_ERROR_MMAL(error, "H264: Can't send MMAL input buffer");
				return -1;
			}
			sent = true;
		}

		error = mmal_wrapper_buffer_get_full(enc->output_port, &out, 0);
		if (error == MMAL_EAGAIN) {
			if (vcos_my_semwait("H264: ", &enc->handler_sem, 1) < 0) {
				return -1;
			}
			continue;
		} else if (error != MMAL_SUCCESS) {
			LOG_ERROR_MMAL(error, "H264: Can't get MMAL output buffer");
			return -1;
		}

		frame_append_data(dest, out->data, out->length);
		dest->key = out->flags & MMAL_BUFFER_HEADER_FLAG_KEYFRAME;

		eos = out->flags & MMAL_BUFFER_HEADER_FLAG_EOS;
		mmal_buffer_header_release(out);
	}

	if ((error = mmal_port_flush(enc->output_port)) != MMAL_SUCCESS) {
		LOG_ERROR_MMAL(error, "H264: Can't flush MMAL output buffer; ignored");
	}
	return 0;
}

static void _mmal_callback(MMAL_WRAPPER_T *wrapper) {
	vcos_semaphore_post(&((h264_encoder_s *)(wrapper->user_data))->handler_sem);
}

static const char *_mmal_error_to_string(MMAL_STATUS_T error) {
	// http://www.jvcref.com/files/PI/documentation/html/group___mmal_types.html
#	define CASE_ERROR(_name, _msg) case MMAL_##_name: return "MMAL_" #_name " [" _msg "]"
	switch (error) {
		case MMAL_SUCCESS: return "MMAL_SUCCESS";
		CASE_ERROR(ENOMEM,		"Out of memory");
		CASE_ERROR(ENOSPC,		"Out of resources");
		CASE_ERROR(EINVAL,		"Invalid argument");
		CASE_ERROR(ENOSYS,		"Function not implemented");
		CASE_ERROR(ENOENT,		"No such file or directory");
		CASE_ERROR(ENXIO,		"No such device or address");
		CASE_ERROR(EIO,			"IO error");
		CASE_ERROR(ESPIPE,		"Illegal seek");
		CASE_ERROR(ECORRUPT,	"Data is corrupt");
		CASE_ERROR(ENOTREADY,	"Component is not ready");
		CASE_ERROR(ECONFIG,		"Component is not configured");
		CASE_ERROR(EISCONN,		"Port is already connected");
		CASE_ERROR(ENOTCONN,	"Port is disconnected");
		CASE_ERROR(EAGAIN,		"Resource temporarily unavailable");
		CASE_ERROR(EFAULT,		"Bad address");
		case MMAL_STATUS_MAX: break; // Makes cpplint happy
	}
	return "Unknown error";
#	undef CASE_ERROR
}

#undef LOG_ERROR_MMAL
