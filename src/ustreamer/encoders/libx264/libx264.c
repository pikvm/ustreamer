#include <stdint.h>
#include <linux/videodev2.h>
#include <string.h>
#include <x264.h>
#include <libyuv.h>
#include "../../../libs/frame.h"
#include "../../../libs/logging.h"

#include "libx264.h"

void us_libx264_encoder_init(us_libx264_encoder_s *enc, int frame_width, int frame_height, uint h264_bitrate, uint h264_gop, char *h264_preset);
int us_libx264_encoder_compress(us_libx264_encoder_s *enc, const us_frame_s *src, us_frame_s *dest, bool force_key);
void us_libx264_encoder_destroy(us_libx264_encoder_s *enc);

bool is_valid_preset(const char* preset) {
    const char *valid_presets[] = {
        "ultrafast", "superfast", "veryfast", 
        "faster", "fast", "medium", 
        "slow", "slower", "veryslow", "placebo"
    };
    int num_presets = sizeof(valid_presets) / sizeof(valid_presets[0]);
    
    for (int i = 0; i < num_presets; ++i) {
        if (strcmp(preset, valid_presets[i]) == 0) {
            return true;
        }
    }
    return false;
}

void us_libx264_encoder_init(us_libx264_encoder_s *enc, int frame_width, int frame_height, uint h264_bitrate, uint h264_gop, char *h264_preset){
    // Initialize libx264 encoding parameters
    US_LOG_INFO("H264 Encoder libx264: Initializing ...");
	enc->param = (x264_param_t *)calloc(1, sizeof(x264_param_t));

	if (h264_preset != NULL && is_valid_preset(h264_preset)) {
        US_LOG_INFO("H264 Encoder libx264: Preset: %s", h264_preset);
        x264_param_default_preset(enc->param, h264_preset, NULL);
    } else {
        US_LOG_INFO("H264 Encoder libx264: Invalid preset,sing default preset: ultrafast");
        x264_param_default_preset(enc->param, "ultrafast", NULL);
    }

    enc->param->i_threads = sysconf(_SC_NPROCESSORS_ONLN); // Automatically set threads based on CPU cores
    enc->param->b_sliced_threads = 0;
    enc->param->i_width = frame_width;
    enc->param->i_height = frame_height;
    enc->param->i_fps_num = 30;
    enc->param->i_fps_den = 1;
    enc->param->i_log_level = X264_LOG_NONE; // Use X264_LOG_DEBUG for debugging
    enc->param->b_repeat_headers = 1;
    enc->param->rc.b_mb_tree = 0;
    enc->param->rc.i_rc_method = X264_RC_ABR;
    enc->param->rc.i_bitrate = h264_bitrate;
    enc->param->rc.i_vbv_max_bitrate = h264_bitrate;
    enc->param->rc.i_vbv_buffer_size = h264_bitrate / 2; // Set a reasonable buffer size
    enc->param->i_keyint_max = h264_gop;

    enc->picture_in = (x264_picture_t*)calloc(1, sizeof(x264_picture_t));	
}

int us_libx264_encoder_compress(us_libx264_encoder_s *enc, const us_frame_s *src, us_frame_s *dest, bool force_key){
    if (!enc->handle) {
        switch (src->format) {
            case V4L2_PIX_FMT_YUYV:
                enc->param->i_csp = X264_CSP_I422;
                US_LOG_INFO("H264 Encoder libx264: Input format is YUYV, using CSP I422");
                break;
            case V4L2_PIX_FMT_YUV420:
                enc->param->i_csp = X264_CSP_I420;
                US_LOG_INFO("H264 Encoder libx264: Input format is YUV420, using CSP I420");
                break;
            case V4L2_PIX_FMT_RGB24:
                enc->param->i_csp = X264_CSP_I420;
                US_LOG_INFO("H264 Encoder libx264: Input format is RGB24, converting to I420");
                break;
            default:
                US_LOG_ERROR("H264 Encoder libx264: Unsupported input format");
                return -1;
        }
        x264_picture_alloc(enc->picture_in, enc->param->i_csp, enc->param->i_width, enc->param->i_height);
        enc->handle = x264_encoder_open(enc->param);
        if (!enc->handle) {
            US_LOG_ERROR("H264 Encoder libx264: Failed to open encoder");
            return -1;
        }
    }

    x264_picture_t picture_out;
    int result = 0, nNal = 0;
    static long int pts = 0;
    uint8_t *p_out = dest->data;

    // Convert formats
    switch (src->format) {
        case V4L2_PIX_FMT_YUYV:
            // Conversion logic here
            break;
        case V4L2_PIX_FMT_YUV420:
            enc->picture_in->img.plane[0] = src->data;
            enc->picture_in->img.plane[1] = src->data + src->width * src->height;
            enc->picture_in->img.plane[2] = src->data + src->width * src->height * 5 / 4;
            break;
        case V4L2_PIX_FMT_RGB24:
            if(RGB24ToI420(src->data, src->width * 3, enc->picture_in->img.plane[0], src->width,
                           enc->picture_in->img.plane[2], src->width / 2, 
                           enc->picture_in->img.plane[1], src->width / 2, src->width, src->height)){
                US_LOG_ERROR("H264 Encoder libx264: RGB24ToI420 conversion failed!");
                return -1;
            }
            break;
    }

    // Determine if the current frame should be a keyframe
    enc->picture_in->i_type = force_key ? X264_TYPE_KEYFRAME : X264_TYPE_AUTO;
    enc->picture_in->i_pts = pts++;

    if (x264_encoder_encode(enc->handle, &enc->nal, &nNal, enc->picture_in, &picture_out) < 0) {
        US_LOG_ERROR("H264 Encoder libx264: Encoding failed!");
        return -1;
    }

    // Copy encoded video frame to destination address and copy metadata
    for (int i = 0; i < nNal; i++) {
        memcpy(p_out, enc->nal[i].p_payload, enc->nal[i].i_payload);   
        p_out += enc->nal[i].i_payload;                                 
        result += enc->nal[i].i_payload;
    }
    US_FRAME_COPY_META(src, dest);
    dest->format = V4L2_PIX_FMT_H264;
    dest->used = result;
    dest->key = (picture_out.i_type == X264_TYPE_IDR || picture_out.i_type == X264_TYPE_I);
    return 0;
}

void us_libx264_encoder_destroy(us_libx264_encoder_s *enc){
    if (enc->handle){
        x264_encoder_close(enc->handle);
        enc->handle = NULL;
    }
    if(enc->picture_in){
        x264_picture_clean(enc->picture_in);
        free(enc->picture_in);
        enc->picture_in = NULL;
    }
    if(enc->param){
        free(enc->param);
        enc->param = NULL;
    }
}