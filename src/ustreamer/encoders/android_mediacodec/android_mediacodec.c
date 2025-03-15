#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/time.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libavutil/log.h>
#include <libyuv.h>
#include "../../../libs/logging.h"
#include "android_mediacodec.h"

static uint8_t *sps_data = NULL;
static int sps_size = 0;
static uint8_t *pps_data = NULL;
static int pps_size = 0;

static void queue_init(us_encoded_frame_queue_s *queue)
{
	queue->head = NULL;
	queue->tail = NULL;
	queue->count = 0;
}

static void queue_push(us_encoded_frame_queue_s *queue, const uint8_t *data, size_t size, bool is_key)
{
    us_encoded_frame_node_s *node = malloc(sizeof(us_encoded_frame_node_s));
    if (!node)
        return;

    node->data = malloc(size);
    if (!node->data)
    {
        free(node);
        return;
    }

    memcpy(node->data, data, size);
    node->size = size;
    node->is_key = is_key;
    node->next = NULL;

    if (queue->tail)
    {
        queue->tail->next = node;
        queue->tail = node;
    }
    else
    {
        queue->head = queue->tail = node;
    }

    queue->count++;
}

static bool queue_pop(us_encoded_frame_queue_s *queue, uint8_t **data, size_t *size, bool *is_key)
{
	if (!queue->head)
		return false;

	us_encoded_frame_node_s *node = queue->head;
	*data = node->data;
	*size = node->size;
	*is_key = node->is_key;

	queue->head = node->next;
	if (!queue->head)
		queue->tail = NULL;

	free(node);
	queue->count--;

	return true;
}

static void queue_clear(us_encoded_frame_queue_s *queue)
{
	while (queue->head)
	{
		us_encoded_frame_node_s *node = queue->head;
		queue->head = node->next;
		free(node->data);
		free(node);
	}
	queue->tail = NULL;
	queue->count = 0;
}

static int extract_extradata(us_android_bridge_encoder_s *enc)
{
	if (!enc->codec_ctx || !enc->codec_ctx->extradata)
		return -1;

	uint8_t *data = enc->codec_ctx->extradata;
	int size = enc->codec_ctx->extradata_size;
	int i = 0;

	if (sps_data)
	{
		free(sps_data);
		sps_data = NULL;
		sps_size = 0;
	}
	if (pps_data)
	{
		free(pps_data);
		pps_data = NULL;
		pps_size = 0;
	}

	while (i + 4 < size)
	{
		if (data[i] == 0 && data[i + 1] == 0 && data[i + 2] == 0 && data[i + 3] == 1)
		{
			int nal_start = i + 4;
			int nal_type = data[nal_start] & 0x1F;

			int j = nal_start;
			while (j + 3 < size)
			{
				if (data[j] == 0 && data[j + 1] == 0 && data[j + 2] == 0 && data[j + 3] == 1)
				{
					break;
				}
				j++;
			}

			int nal_size = (j + 3 < size) ? j - nal_start : size - nal_start;

			if (nal_type == 7)
			{
				sps_size = nal_size + 4;
				sps_data = malloc(sps_size);
				if (sps_data)
				{
					memcpy(sps_data, &data[i], sps_size);
					US_LOG_INFO("Android MediaCodec: SPS extracted, size=%d", sps_size);
				}
			}
			else if (nal_type == 8)
			{
				pps_size = nal_size + 4;
				pps_data = malloc(pps_size);
				if (pps_data)
				{
					memcpy(pps_data, &data[i], pps_size);
					US_LOG_INFO("Android MediaCodec: PPS extracted, size=%d", pps_size);
				}
			}

			i = (j + 3 < size) ? j : size;
		}
		else
		{
			i++;
		}
	}

	return (sps_data && pps_data) ? 0 : -1;
}

static int process_received_packet(us_android_bridge_encoder_s *enc, AVPacket *pkt)
{
	if (!pkt || pkt->size <= 0)
		return -1;
	queue_push(&enc->queue, pkt->data, pkt->size, (pkt->flags & AV_PKT_FLAG_KEY) ? true : false);
	return 0;
}

static int receive_all_packets(us_android_bridge_encoder_s *enc)
{
	int ret = 0;
	int received = 0;

	AVPacket *pkt = av_packet_alloc();
	if (!pkt)
	{
		US_LOG_ERROR("Android MediaCodec: Failed to allocate packet");
		return -1;
	}

	while (1)
	{
		ret = avcodec_receive_packet(enc->codec_ctx, pkt);
		if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
		{
			break;
		}
		else if (ret < 0)
		{
			char err_buf[AV_ERROR_MAX_STRING_SIZE] = {0};
			av_strerror(ret, err_buf, AV_ERROR_MAX_STRING_SIZE);
			US_LOG_ERROR("Android MediaCodec: Failed to receive packet: %s", err_buf);
			av_packet_free(&pkt);
			return -1;
		}

		process_received_packet(enc, pkt);
		received++;

		av_packet_unref(pkt);
	}

	av_packet_free(&pkt);
	return received;
}

static int validate_nal_units(us_android_bridge_encoder_s *enc, uint8_t *data, size_t size)
{
    if (size < 4)
        return 0;

    if ((data[0] == 0 && data[1] == 0 && data[2] == 0 && data[3] == 1) ||
        (data[0] == 0 && data[1] == 0 && data[2] == 1))
        return 0;

    if (size + 4 > enc->nal_buffer_size) {
        enc->nal_buffer_size = size + 4 + 1024;
        free(enc->nal_buffer);
        posix_memalign((void**)&enc->nal_buffer, 16, enc->nal_buffer_size);
    }

    enc->nal_buffer[0] = 0;
    enc->nal_buffer[1] = 0;
    enc->nal_buffer[2] = 0;
    enc->nal_buffer[3] = 1;

    memcpy(enc->nal_buffer + 4, data, size);
    memcpy(data, enc->nal_buffer, size + 4);
    
    return 4;
}

void us_android_mediacodec_init(us_android_bridge_encoder_s *enc, int frame_width, int frame_height, uint h264_bitrate, uint h264_gop)
{
	US_LOG_INFO("Android MediaCodec: Initializing with %dx%d, bitrate=%u, gop=%u",
				frame_width, frame_height, h264_bitrate, h264_gop);

	int ret = 0;
	memset(enc, 0, sizeof(us_android_bridge_encoder_s));
	queue_init(&enc->queue);

	enc->frame_width = frame_width;
	enc->frame_height = frame_height;
	enc->bitrate = h264_bitrate;
	enc->gop = h264_gop;
	enc->frame_count = 0;

	const AVCodec *codec = avcodec_find_encoder_by_name("h264_mediacodec");
	if (!codec)
	{
		US_LOG_ERROR("Android MediaCodec: h264_mediacodec not found");
		return;
	}
	enc->codec_ctx = avcodec_alloc_context3(codec);
	enc->codec_ctx->width = frame_width;
	enc->codec_ctx->height = frame_height;
	if ((frame_width <= 1280 && frame_height <= 720)) {
		enc->codec_ctx->time_base = (AVRational){1, 60};
		enc->codec_ctx->framerate = (AVRational){60, 1};
	} else {
		enc->codec_ctx->time_base = (AVRational){1, 30};
		enc->codec_ctx->framerate = (AVRational){30, 1};
	}
	enc->codec_ctx->bit_rate = h264_bitrate * 1000;
	enc->codec_ctx->gop_size = h264_gop;
	enc->codec_ctx->max_b_frames = 0;
	enc->codec_ctx->pix_fmt = AV_PIX_FMT_NV12;

	AVDictionary *codec_options = NULL;
	av_dict_set(&codec_options, "tune", "zerolatency", 0);
	av_dict_set_int(&codec_options, "bitrate_mode", 1, 0);
	ret = avcodec_open2(enc->codec_ctx, codec, &codec_options);
	if (ret < 0)
	{
		char err_buf[AV_ERROR_MAX_STRING_SIZE] = {0};
		av_strerror(ret, err_buf, AV_ERROR_MAX_STRING_SIZE);
		US_LOG_ERROR("Android MediaCodec: Failed to open codec: %s", err_buf);
		avcodec_free_context(&enc->codec_ctx);
		enc->codec_ctx = NULL;
		return;
	}

	enc->frame = av_frame_alloc();
	enc->frame->format = enc->codec_ctx->pix_fmt;
	enc->frame->width = enc->codec_ctx->width;
	enc->frame->height = enc->codec_ctx->height;
	enc->force_first_iframe = 1;
	av_frame_get_buffer(enc->frame, 32);
	extract_extradata(enc);

	size_t y_size = frame_width * frame_height;
	size_t uv_size = y_size / 4;
	posix_memalign((void**)&enc->rgb_conv_y, 16, y_size);
	posix_memalign((void**)&enc->rgb_conv_u, 16, uv_size);
	posix_memalign((void**)&enc->rgb_conv_v, 16, uv_size);
	enc->nal_buffer_size = frame_width * frame_height * 3;
	posix_memalign((void**)&enc->nal_buffer, 16, enc->nal_buffer_size);
	enc->keyframe_buffer_size = frame_width * frame_height * 3;
	posix_memalign((void**)&enc->keyframe_buffer, 16, enc->keyframe_buffer_size);

	US_LOG_INFO("Android MediaCodec: Initialization completed successfully");
}

int us_android_mediacodec_compress(us_android_bridge_encoder_s *enc, const us_frame_s *src, us_frame_s *dest, bool force_key)
{
	if (!enc->codec_ctx || !enc->frame)
	{
		US_LOG_ERROR("Android MediaCodec: Encoder not initialized");
		return -1;
	}

	int ret = 0;
	size_t size = 0;
	bool is_key = false;
	uint8_t *data = NULL;
	av_frame_make_writable(enc->frame);

	switch (src->format)
	{
	case V4L2_PIX_FMT_YUV420:
		ret = I420ToNV12(src->data,enc->frame_width,src->data + enc->frame_width * enc->frame_height,enc->frame_width / 2,src->data + enc->frame_width * enc->frame_height * 5 / 4,
			enc->frame_width / 2,enc->frame->data[0],enc->frame->linesize[0],enc->frame->data[1],enc->frame->linesize[1],enc->frame_width,enc->frame_height);
		break;

	case V4L2_PIX_FMT_YUYV:
		ret = YUY2ToNV12(src->data,enc->frame_width * 2,enc->frame->data[0],enc->frame->linesize[0],enc->frame->data[1],enc->frame->linesize[1],enc->frame_width,enc->frame_height);
		break;

	case V4L2_PIX_FMT_RGB24:
	{
		ret = RGB24ToI420(src->data, enc->frame_width * 3,enc->rgb_conv_y, enc->frame_width,enc->rgb_conv_u, enc->frame_width / 2,enc->rgb_conv_v, enc->frame_width / 2,enc->frame_width, enc->frame_height);
		memcpy(enc->frame->data[0], enc->rgb_conv_y, enc->frame_width * enc->frame_height);
		MergeUVPlane(enc->rgb_conv_v, enc->frame_width / 2,enc->rgb_conv_u, enc->frame_width / 2,enc->frame->data[1], enc->frame->linesize[1],enc->frame_width / 2, enc->frame_height / 2);
	}
	break;

	default:
		US_LOG_ERROR("Android MediaCodec: Unsupported input format: %u", src->format);
		return -1;
	}

	if (ret < 0)
	{
		US_LOG_ERROR("Android MediaCodec: Failed to convert frame format: %d", ret);
		return -1;
	}

	if (enc->frame_count == 0 || force_key || (enc->frame_count % enc->gop == 0))
	{
		enc->frame->pict_type = AV_PICTURE_TYPE_I;
		enc->force_first_iframe = 0;
	}
	else
	{
		enc->frame->pict_type = AV_PICTURE_TYPE_NONE;
	}
	enc->frame->pts = enc->frame_count++;

	ret = avcodec_send_frame(enc->codec_ctx, enc->frame);
	if (ret == AVERROR(EAGAIN))
	{
		US_LOG_VERBOSE("Android MediaCodec: Buffer full, receiving packets first");
		int received = receive_all_packets(enc);

		if (received <= 0)
		{
			uint8_t *data = NULL;
			size_t size = 0;
			bool is_key = false;

			if (queue_pop(&enc->queue, &data, &size, &is_key))
			{
				goto return_frame;
			}
			return 0;
		}
	}
	else if (ret < 0)
	{
		char err_buf[AV_ERROR_MAX_STRING_SIZE] = {0};
		av_strerror(ret, err_buf, AV_ERROR_MAX_STRING_SIZE);
		US_LOG_ERROR("Android MediaCodec: Failed to send frame: %s", err_buf);

		if (queue_pop(&enc->queue, &data, &size, &is_key))
		{
			goto return_frame;
		}
		return -1;
	}

	receive_all_packets(enc);

	if (queue_pop(&enc->queue, &data, &size, &is_key))
	{
		goto return_frame;
	}

	return 0;

return_frame:
	if (is_key && sps_data && pps_data)
	{
		size_t total_size = size + sps_size + pps_size;
        
        if (total_size > enc->keyframe_buffer_size) {
            enc->keyframe_buffer_size = total_size + 1024;
            free(enc->keyframe_buffer);
            posix_memalign((void**)&enc->keyframe_buffer, 16, enc->keyframe_buffer_size);
        }
        
        memcpy(enc->keyframe_buffer, sps_data, sps_size);
        memcpy(enc->keyframe_buffer + sps_size, pps_data, pps_size);
        memcpy(enc->keyframe_buffer + sps_size + pps_size, data, size);
        free(data);
        data = enc->keyframe_buffer;
        size = total_size;
        US_LOG_VERBOSE("Android MediaCodec: Added SPS and PPS to keyframe");
	}
	else if (is_key && (!sps_data || !pps_data))
	{
		uint8_t *frame_data = data;
		int frame_size = size;
		int i = 0;

		while (i + 4 < frame_size)
		{
			if ((frame_data[i] == 0 && frame_data[i + 1] == 0 && frame_data[i + 2] == 0 && frame_data[i + 3] == 1) ||
				(frame_data[i] == 0 && frame_data[i + 1] == 0 && frame_data[i + 2] == 1))
			{

				int start_code_len = (frame_data[i + 2] == 1) ? 3 : 4;
				int nal_start = i + start_code_len;
				int nal_type = frame_data[nal_start] & 0x1F;

				if (nal_type == 7 && !sps_data)
				{
					int j = nal_start;
					while (j + 3 < frame_size)
					{
						if ((frame_data[j] == 0 && frame_data[j + 1] == 0 && frame_data[j + 2] == 0 && frame_data[j + 3] == 1) ||
							(frame_data[j] == 0 && frame_data[j + 1] == 0 && frame_data[j + 2] == 1))
						{
							break;
						}
						j++;
					}

					sps_size = j - i;
					sps_data = malloc(sps_size);
					if (sps_data)
					{
						memcpy(sps_data, &frame_data[i], sps_size);
						US_LOG_VERBOSE("Android MediaCodec: SPS extracted from keyframe, size=%d", sps_size);
					}
				}
				else if (nal_type == 8 && !pps_data)
				{
					int j = nal_start;
					while (j + 3 < frame_size)
					{
						if ((frame_data[j] == 0 && frame_data[j + 1] == 0 && frame_data[j + 2] == 0 && frame_data[j + 3] == 1) ||
							(frame_data[j] == 0 && frame_data[j + 1] == 0 && frame_data[j + 2] == 1))
						{
							break;
						}
						j++;
					}

					pps_size = j - i;
					pps_data = malloc(pps_size);
					if (pps_data)
					{
						memcpy(pps_data, &frame_data[i], pps_size);
						US_LOG_VERBOSE("Android MediaCodec: PPS extracted from keyframe, size=%d", pps_size);
					}
				}

				i = nal_start;
			}
			else
			{
				i++;
			}
		}
	}

	int size_change = validate_nal_units(enc, data, size);
    if (size_change > 0)
    {
        size += size_change;
    }

	if (dest->allocated < size)
    {
        void *new_data = realloc(dest->data, size);
        dest->data = new_data;
        dest->allocated = size;
    }

	US_FRAME_COPY_META(src, dest);
	memcpy(dest->data, data, size);
	dest->key = is_key ? 1 : 0;
	dest->format = V4L2_PIX_FMT_H264;
	dest->used = size;

	if (data != enc->keyframe_buffer)
    {
        free(data);
    }

	return 0;
}

void us_android_mediacodec_destroy(us_android_bridge_encoder_s *enc)
{
	queue_clear(&enc->queue);

	if (enc->rgb_conv_y) {
		free(enc->rgb_conv_y);
		enc->rgb_conv_y = NULL;
	}

	if (enc->rgb_conv_u) {
		free(enc->rgb_conv_u);
		enc->rgb_conv_u = NULL;
	}

	if (enc->rgb_conv_v) {
		free(enc->rgb_conv_v);
		enc->rgb_conv_v = NULL;
	}

	if (enc->nal_buffer) {
		free(enc->nal_buffer);
		enc->nal_buffer = NULL;
	}

	if (enc->keyframe_buffer) {
		free(enc->keyframe_buffer);
		enc->keyframe_buffer = NULL;
	}

	if (sps_data)
	{
		free(sps_data);
		sps_data = NULL;
		sps_size = 0;
	}

	if (pps_data)
	{
		free(pps_data);
		pps_data = NULL;
		pps_size = 0;
	}

	if (enc->frame)
	{
		av_frame_free(&enc->frame);
		enc->frame = NULL;
	}

	if (enc->codec_ctx)
	{
		avcodec_free_context(&enc->codec_ctx);
		enc->codec_ctx = NULL;
	}

	if (enc->pkt)
	{
		av_packet_free(&enc->pkt);
		enc->pkt = NULL;
	}
	enc->frame_count = 0;
	US_LOG_INFO("Android MediaCodec: Encoder destroyed");
}