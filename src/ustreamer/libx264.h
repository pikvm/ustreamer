#pragma once
#include <stdint.h>
#include "../libs/x264.h"
#include "../libs/frame.h"

typedef struct {
	x264_param_t *param;
	x264_t *handle;
	x264_picture_t *picture_in;
	x264_picture_t *picture_out;
	x264_nal_t *nal;
} us_libx264_encoder_s;

void us_libx264_encoder_init(us_libx264_encoder_s *enc, int frame_width, int frame_height);
int us_libx264_encoder_compress(us_libx264_encoder_s *enc, const us_frame_s *src, us_frame_s *dest, bool force_key);
void us_libx264_encoder_destroy(us_libx264_encoder_s *enc);