
#include <stdint.h>
#include <linux/videodev2.h>
#include "../../../libs/x264.h"
#include "../../../libs/frame.h"
#include "../../../libs/logging.h"

#include "libx264.h"


void us_libx264_encoder_init(us_libx264_encoder_s *enc, int frame_width, int frame_height, uint h264_bitrate, uint h264_gop);
int us_libx264_encoder_compress(us_libx264_encoder_s *enc, const us_frame_s *src, us_frame_s *dest, bool force_key);
void us_libx264_encoder_destroy(us_libx264_encoder_s *enc);

void us_libx264_encoder_init(us_libx264_encoder_s *enc, int frame_width, int frame_height, uint h264_bitrate, uint h264_gop){
	US_LOG_INFO("H264: Encoder libx264 Initializing ...");
	US_LOG_INFO("libx264 param: frame_width=%d,frame_height=%d,h264_bitrate=%u,h264_gop=%u",frame_width, frame_height, h264_bitrate, h264_gop);
    enc->param = (x264_param_t *)malloc(sizeof(x264_param_t));
    x264_param_default(enc->param);
	x264_param_default_preset(enc->param, "ultrafast", "zerolatency");
    enc->param->i_threads = X264_SYNC_LOOKAHEAD_AUTO;
    enc->param->i_width = frame_width;
    enc->param->i_height = frame_height;
    enc->param->i_fps_num = 30;
    enc->param->i_fps_den = 1;
    enc->param->i_csp = X264_CSP_I422;//X264_CSP_BGR,X264_CSP_I420
    enc->param->i_log_level = X264_LOG_INFO;//X264_LOG_DEBUG,X264_LOG_NONE
	enc->param->b_repeat_headers = 1; 
	enc->param->rc.b_mb_tree=0;//设置实时编码
	enc->param->rc.i_rc_method = X264_RC_ABR;
	enc->param->rc.i_bitrate = h264_bitrate / 1000;
	// 设置最大码率, 单位 kbps, 该配置与 i_vbv_buffer_size 配套使用
	enc->param->rc.i_vbv_max_bitrate = h264_bitrate / 1000 * 1.2;
	// 该配置与 i_vbv_max_bitrate 配置配套使用, 码率控制缓冲区大小
	enc->param->rc.i_vbv_buffer_size = h264_bitrate / 1000;
	enc->param->i_keyint_max = h264_gop;
	//enc->param->pf_log
	//x264_param_apply_profile(enc->param, x264_profile_names[4]);
	enc->handle = x264_encoder_open(enc->param);
    assert(enc->handle);
	enc->picture_in = (x264_picture_t*)malloc(sizeof(x264_picture_t));
    x264_picture_alloc(enc->picture_in, enc->param->i_csp, enc->param->i_width, enc->param->i_height);
}

int us_libx264_encoder_compress(us_libx264_encoder_s *enc, const us_frame_s *src, us_frame_s *dest, bool force_key){
	x264_picture_t picture_out;


    int index_y=0;int index_u=0;int index_v = 0;
	int num = src->width * src->height * 2 - 4  ;
	int nNal = -1;
	int result = 0;
	static long int pts = 0;
	uint8_t *p_out = dest->data;
    uint8_t *y = enc->picture_in->img.plane[0];   
	uint8_t *u = enc->picture_in->img.plane[1];   
	uint8_t *v = enc->picture_in->img.plane[2];
	for(int i=0; i<num+4; i=i+4) {
			*(y + (index_y++)) = *(src->data + i);
			*(u + (index_u++)) = *(src->data + i + 1);
			*(y + (index_y++)) = *(src->data + i + 2);
			*(v + (index_v++)) = *(src->data + i + 3);
	}
	if (force_key)
	{
		enc->picture_in->i_type = X264_TYPE_KEYFRAME;
	}else
	{
		enc->picture_in->i_type = X264_TYPE_AUTO;
	}
	
	
	
	enc->picture_in->i_pts = pts++;
 
	if (x264_encoder_encode(enc->handle, &(enc->nal), &nNal, enc->picture_in,
			&picture_out) < 0) {
		return -1;
	}

	
    
	for (int i = 0; i < nNal; i++) {
		memcpy(p_out, enc->nal[i].p_payload, enc->nal[i].i_payload);   
		p_out += enc->nal[i].i_payload;								 
		result += enc->nal[i].i_payload;
	}
    US_FRAME_COPY_META(src, dest);
	dest->format = V4L2_PIX_FMT_H264;
	dest->used = result;
    return 0;
}

void us_libx264_encoder_destroy(us_libx264_encoder_s *enc){
	if (enc->handle){
		x264_encoder_close(enc->handle);
	}
	if(enc->picture_in){
		x264_picture_clean(enc->picture_in);
		free(enc->picture_in);
	}
	if(enc->param){
		free(enc->param);
	}
	
	
}
