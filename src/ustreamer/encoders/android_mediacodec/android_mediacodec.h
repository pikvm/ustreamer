#ifndef US_ANDROID_BRIDGE_H
#define US_ANDROID_BRIDGE_H

#include <stdint.h>
#include <stdbool.h>
#include <linux/videodev2.h>

#include "../../../libs/frame.h"

struct AVCodecContext;
struct AVFrame;
struct AVPacket;
struct SwsContext;

typedef struct us_encoded_frame_node {
    uint8_t *data;
    size_t size;
    bool is_key;
    struct us_encoded_frame_node *next;
} us_encoded_frame_node_s;

typedef struct {
    us_encoded_frame_node_s *head;
    us_encoded_frame_node_s *tail;
    int count;
} us_encoded_frame_queue_s;

typedef struct {
    struct AVCodecContext *codec_ctx;
    struct AVFrame *frame;
    struct AVPacket *pkt;
    struct SwsContext *sws_ctx;
    int frame_width;
    int frame_height;
    uint bitrate;
    uint gop;
    int frame_count;
    us_encoded_frame_queue_s queue;
	int force_first_iframe;

	uint8_t *rgb_conv_y;
    uint8_t *rgb_conv_u;
    uint8_t *rgb_conv_v;
    uint8_t *nal_buffer;
    size_t nal_buffer_size;
    uint8_t *keyframe_buffer;
    size_t keyframe_buffer_size;
} us_android_bridge_encoder_s;

void us_android_mediacodec_init(us_android_bridge_encoder_s *enc, int frame_width, int frame_height, uint h264_bitrate, uint h264_gop);
int us_android_mediacodec_compress(us_android_bridge_encoder_s *enc, const us_frame_s *src, us_frame_s *dest, bool force_key);
void us_android_mediacodec_destroy(us_android_bridge_encoder_s *enc);

#endif
