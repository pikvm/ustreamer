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


static int _h264_encoder_configure(h264_encoder_s *encoder, const frame_s *frame);
static void _h264_encoder_cleanup(h264_encoder_s *encoder);

static int _h264_encoder_compress_raw(h264_encoder_s *encoder, const frame_s *src, frame_s *dest, bool force_key);

static void _mmal_callback(MMAL_WRAPPER_T *wrapper);
static const char *_mmal_error_to_string(MMAL_STATUS_T error);


#define RUN(_next) encoder->run->_next

#define LOG_ERROR_MMAL(_error, _msg, ...) { \
		LOG_ERROR(_msg ": %s", ##__VA_ARGS__, _mmal_error_to_string(_error)); \
	}


h264_encoder_s *h264_encoder_init(void) {
	h264_encoder_runtime_s *run;
	A_CALLOC(run, 1);
	run->unjpegged = frame_init("h264_unjpegged_src");
	run->last_online = -1;

	h264_encoder_s *encoder;
	A_CALLOC(encoder, 1);
	encoder->gop = 60;
	encoder->bps = 5000 * 1000; // Kbps * 1000
	encoder->fps = 30;
	encoder->run = run;

	if (vcos_semaphore_create(&run->handler_sem, "h264_handler_sem", 0) != VCOS_SUCCESS) {
		LOG_PERROR("Can't create VCOS semaphore");
		goto error;
	}
	run->i_handler_sem = true;

	return encoder;

	error:
		h264_encoder_destroy(encoder);
		return NULL;
}

void h264_encoder_destroy(h264_encoder_s *encoder) {
	_h264_encoder_cleanup(encoder);
	if (RUN(i_handler_sem)) {
		vcos_semaphore_delete(&RUN(handler_sem));
	}
	frame_destroy(RUN(unjpegged));
	free(encoder);
}

int h264_encoder_compress(h264_encoder_s *encoder, const frame_s *src, frame_s *dest) {
	assert(src->used > 0);
	assert(src->width > 0);
	assert(src->height > 0);
	assert(src->format > 0);

	if (src->format == V4L2_PIX_FMT_MJPEG || src->format == V4L2_PIX_FMT_JPEG) {
		LOG_DEBUG("Input frame format is JPEG; decoding ...");
		if (unjpeg(src, RUN(unjpegged), true) < 0) {
			return -1;
		}
		src = RUN(unjpegged);
	}

	if (RUN(i_width) != src->width || RUN(i_height) != src->height || RUN(i_format) != src->format) {
		if (_h264_encoder_configure(encoder, src) < 0) {
			return -1;
		}
		RUN(last_online) = -1;
	}

	if (_h264_encoder_compress_raw(encoder, src, dest, (RUN(last_online) != src->online)) < 0) {
		_h264_encoder_cleanup(encoder);
		return -1;
	}

	RUN(last_online) = src->online;
	return 0;
}

static int _h264_encoder_configure(h264_encoder_s *encoder, const frame_s *frame) {
	MMAL_STATUS_T error;

#	define PREPARE_PORT(_id) { \
			RUN(_id##_port) = RUN(wrapper->_id[0]); \
			if (RUN(_id##_port->is_enabled)) { \
				if ((error = mmal_wrapper_port_disable(RUN(_id##_port))) != MMAL_SUCCESS) { \
					LOG_ERROR_MMAL(error, "Can't disable MMAL %s port while configuring", #_id); \
					goto error; \
				} \
			} \
		}

#	define COMMIT_PORT(_id) { \
			if ((error = mmal_port_format_commit(RUN(_id##_port))) != MMAL_SUCCESS) { \
				LOG_ERROR_MMAL(error, "Can't commit MMAL %s port", #_id); \
				goto error; \
			} \
		}

#	define SET_PORT_PARAM(_id, _type, _key, _value) { \
			if ((error = mmal_port_parameter_set_##_type(RUN(_id##_port), _key, _value)) != MMAL_SUCCESS) { \
				LOG_ERROR_MMAL(error, "Can't set %s for the %s port", #_key, #_id); \
				goto error; \
			} \
		}

#	define ENABLE_PORT(_id) { \
			if ((error = mmal_wrapper_port_enable(RUN(_id##_port), MMAL_WRAPPER_FLAG_PAYLOAD_ALLOCATE)) != MMAL_SUCCESS) { \
				LOG_ERROR_MMAL(error, "Can't enable MMAL %s port", #_id); \
				goto error; \
			} \
		}

	_h264_encoder_cleanup(encoder);

	if ((error = mmal_wrapper_create(&RUN(wrapper), MMAL_COMPONENT_DEFAULT_VIDEO_ENCODER)) != MMAL_SUCCESS) {
		LOG_ERROR_MMAL(error, "Can't create MMAL wrapper");
		goto error;
	}
	RUN(wrapper->status) = MMAL_SUCCESS;

	{
		PREPARE_PORT(input);

#		define IFMT(_next) RUN(input_port->format->_next)
		IFMT(type) = MMAL_ES_TYPE_VIDEO;
		char fourcc_buf[8];
		switch (frame->format) {
			case V4L2_PIX_FMT_YUYV: IFMT(encoding) = MMAL_ENCODING_YUYV; break;
			case V4L2_PIX_FMT_UYVY: IFMT(encoding) = MMAL_ENCODING_UYVY; break;
			case V4L2_PIX_FMT_RGB565: IFMT(encoding) = MMAL_ENCODING_RGB16; break;
			case V4L2_PIX_FMT_RGB24: IFMT(encoding) = MMAL_ENCODING_RGB24; break;
			default:
				LOG_ERROR("Unsupported input format for MMAL (fourcc): %s", fourcc_to_string(frame->format, fourcc_buf, 8));
				goto error;
		}
		IFMT(es->video.width) = align_size(frame->width, 32);
		IFMT(es->video.height) = align_size(frame->height, 16);
		IFMT(es->video.crop.x) = 0;
		IFMT(es->video.crop.y) = 0;
		IFMT(es->video.crop.width) = frame->width;
		IFMT(es->video.crop.height) = frame->height;
		IFMT(flags) = MMAL_ES_FORMAT_FLAG_FRAMED;
		RUN(input_port->buffer_size) = 1000 * 1000;
		RUN(input_port->buffer_num) = RUN(input_port->buffer_num_recommended) * 4;
#		undef IFMT

		COMMIT_PORT(input);
		SET_PORT_PARAM(input, boolean, MMAL_PARAMETER_ZERO_COPY, MMAL_FALSE);
	}

	{
		PREPARE_PORT(output);

#		define OFMT(_next) RUN(output_port->format->_next)
		OFMT(type) = MMAL_ES_TYPE_VIDEO;
		OFMT(encoding) = MMAL_ENCODING_H264;
		OFMT(encoding_variant) = MMAL_ENCODING_VARIANT_H264_DEFAULT;
		OFMT(bitrate) = encoder->bps;
		OFMT(es->video.frame_rate.num) = encoder->fps;
		OFMT(es->video.frame_rate.den) = 1;
		RUN(output_port->buffer_size) = RUN(output_port->buffer_size_recommended) * 4;
		RUN(output_port->buffer_num) = RUN(output_port->buffer_num_recommended);
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
			if ((error = mmal_port_parameter_set(RUN(output_port), &profile.hdr)) != MMAL_SUCCESS) {
				LOG_ERROR_MMAL(error, "Can't set MMAL_PARAMETER_PROFILE for the output port");
				goto error;
			}
		}

		SET_PORT_PARAM(output, boolean, MMAL_PARAMETER_ZERO_COPY, MMAL_TRUE);
		SET_PORT_PARAM(output, uint32, MMAL_PARAMETER_INTRAPERIOD, encoder->gop);
		SET_PORT_PARAM(output, uint32, MMAL_PARAMETER_NALUNITFORMAT, MMAL_VIDEO_NALUNITFORMAT_STARTCODES);
		SET_PORT_PARAM(output, boolean, MMAL_PARAMETER_MINIMISE_FRAGMENTATION, MMAL_TRUE);
		SET_PORT_PARAM(output, uint32, MMAL_PARAMETER_MB_ROWS_PER_SLICE, 0);
		SET_PORT_PARAM(output, boolean, MMAL_PARAMETER_VIDEO_IMMUTABLE_INPUT, MMAL_FALSE);
		SET_PORT_PARAM(output, boolean, MMAL_PARAMETER_VIDEO_DROPPABLE_PFRAMES, MMAL_FALSE);
		SET_PORT_PARAM(output, uint32, MMAL_PARAMETER_VIDEO_BIT_RATE, encoder->bps);
		SET_PORT_PARAM(output, uint32, MMAL_PARAMETER_VIDEO_ENCODE_PEAK_RATE, encoder->bps);
		SET_PORT_PARAM(output, uint32, MMAL_PARAMETER_VIDEO_ENCODE_MIN_QUANT, 16);
		SET_PORT_PARAM(output, uint32, MMAL_PARAMETER_VIDEO_ENCODE_MAX_QUANT, 34);
		SET_PORT_PARAM(output, uint32, MMAL_PARAMETER_VIDEO_ENCODE_FRAME_LIMIT_BITS, 1000000);
		SET_PORT_PARAM(output, uint32, MMAL_PARAMETER_VIDEO_ENCODE_H264_AU_DELIMITERS, MMAL_FALSE);
	}

	RUN(wrapper->user_data) = (void *)encoder;
	RUN(wrapper->callback) = _mmal_callback;

	ENABLE_PORT(input);
	ENABLE_PORT(output);

	RUN(i_width) = frame->width;
	RUN(i_height) = frame->height;
	RUN(i_format) = frame->format;

	return 0;

	error:
		_h264_encoder_cleanup(encoder);
		return -1;

#	undef ENABLE_PORT
#	undef SET_PORT_PARAM
#	undef COMMIT_PORT
#	undef PREPARE_PORT
}

static void _h264_encoder_cleanup(h264_encoder_s *encoder) {
	MMAL_STATUS_T error;

#	define DISABLE_PORT(_id) { \
			if (RUN(_id##_port)) { \
				if ((error = mmal_wrapper_port_disable(RUN(_id##_port))) != MMAL_SUCCESS) { \
					LOG_ERROR_MMAL(error, "Can't disable MMAL %s port", #_id); \
				} \
				RUN(_id##_port) = NULL; \
			} \
		}

	DISABLE_PORT(input);
	DISABLE_PORT(output);

#	undef DISABLE_PORT

	if (RUN(wrapper)) {
		if ((error = mmal_wrapper_destroy(RUN(wrapper))) != MMAL_SUCCESS) {
			LOG_ERROR_MMAL(error, "Can't destroy MMAL encoder");
		}
		RUN(wrapper) = NULL;
	}

	RUN(i_width) = 0;
	RUN(i_height) = 0;
	RUN(i_format) = 0;
}

static int _h264_encoder_compress_raw(h264_encoder_s *encoder, const frame_s *src, frame_s *dest, bool force_key) {
	assert(src->used > 0);
	assert(src->width == RUN(i_width));
	assert(src->height == RUN(i_height));
	assert(src->format == RUN(i_format));

	MMAL_STATUS_T error;

	LOG_DEBUG("Compressing new H264 frame; force_key=%d ...", force_key);

	frame_copy_meta(src, dest);
	dest->format = V4L2_PIX_FMT_H264;
	dest->encode_begin_ts = get_now_monotonic();
	dest->used = 0;

	if (force_key) {
		if ((error = mmal_port_parameter_set_boolean(
			RUN(output_port),
			MMAL_PARAMETER_VIDEO_REQUEST_I_FRAME,
			MMAL_TRUE
		)) != MMAL_SUCCESS) {
			LOG_ERROR_MMAL(error, "Can't request keyframe");
			return -1;
		}
	}

	MMAL_BUFFER_HEADER_T *out = NULL;
	MMAL_BUFFER_HEADER_T *in = NULL;
	bool eos = false;
	bool sent = false;

	while (!eos) {
		out = NULL;
		while (mmal_wrapper_buffer_get_empty(RUN(output_port), &out, 0) == MMAL_SUCCESS) {
			if ((error = mmal_port_send_buffer(RUN(output_port), out)) != MMAL_SUCCESS) {
				LOG_ERROR_MMAL(error, "Can't send MMAL output buffer");
				return -1;
			}
		}

		in = NULL;
		if (!sent && mmal_wrapper_buffer_get_empty(RUN(input_port), &in, 0) == MMAL_SUCCESS) {
			in->data = src->data;
			in->length = src->used;
			in->offset = 0;
			in->flags = MMAL_BUFFER_HEADER_FLAG_EOS;
			if ((error = mmal_port_send_buffer(RUN(input_port), in)) != MMAL_SUCCESS) {
				LOG_ERROR_MMAL(error, "Can't send MMAL input buffer");
				return -1;
			}
			sent = true;
		}

		error = mmal_wrapper_buffer_get_full(RUN(output_port), &out, 0);
		if (error == MMAL_EAGAIN) {
			vcos_semaphore_wait(&RUN(handler_sem));
			continue;
		} else if (error != MMAL_SUCCESS) {
			LOG_ERROR_MMAL(error, "Can't get MMAL output buffer");
			return -1;
		}

		frame_append_data(dest, out->data, out->length);

		eos = out->flags & MMAL_BUFFER_HEADER_FLAG_EOS;
		mmal_buffer_header_release(out);
	}

	if ((error = mmal_port_flush(RUN(output_port))) != MMAL_SUCCESS) {
		LOG_ERROR_MMAL(error, "Can't flush MMAL output buffer; ignored");
	}

	dest->encode_end_ts = get_now_monotonic();
	LOG_VERBOSE("Compressed new H264 frame: size=%zu, time=%0.3Lf, force_key=%d",
		dest->used, dest->encode_end_ts - dest->encode_begin_ts, force_key);
	return 0;
}

static void _mmal_callback(MMAL_WRAPPER_T *wrapper) {
	vcos_semaphore_post(&((h264_encoder_s *)(wrapper->user_data))->run->handler_sem);
}

static const char *_mmal_error_to_string(MMAL_STATUS_T error) {
	// http://www.jvcref.com/files/PI/documentation/html/group___mmal_types.html
#	define CASE_ERROR(_name, _msg) case MMAL_##_name: return "MMAL_" #_name " [" _msg "]"
	switch (error) {
		case MMAL_SUCCESS: return "MMAL_SUCCESS";
		CASE_ERROR(ENOMEM, "Out of memory");
		CASE_ERROR(ENOSPC, "Out of resources");
		CASE_ERROR(EINVAL, "Invalid argument");
		CASE_ERROR(ENOSYS, "Function not implemented");
		CASE_ERROR(ENOENT, "No such file or directory");
		CASE_ERROR(ENXIO, "No such device or address");
		CASE_ERROR(EIO, "IO error");
		CASE_ERROR(ESPIPE, "Illegal seek");
		CASE_ERROR(ECORRUPT, "Data is corrupt");
		CASE_ERROR(ENOTREADY, "Component is not ready");
		CASE_ERROR(ECONFIG, "Component is not configured");
		CASE_ERROR(EISCONN, "Port is already connected");
		CASE_ERROR(ENOTCONN, "Port is disconnected");
		CASE_ERROR(EAGAIN, "Resource temporarily unavailable");
		CASE_ERROR(EFAULT, "Bad address");
		case MMAL_STATUS_MAX: break; // Makes cpplint happy
	}
	return "Unknown error";
#	undef CASE_ERROR
}

#undef LOG_ERROR_MMAL
#undef RUN
