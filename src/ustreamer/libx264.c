
#include <stdint.h>
#include "../libs/x264.h"
#include "../libs/frame.h"

#include "libx264.h"


void us_libx264_encoder_init(us_libx264_encoder_s *enc, int frame_width, int frame_height);
int us_libx264_encoder_compress(us_libx264_encoder_s *enc, const us_frame_s *src, us_frame_s *dest, bool force_key);
void us_libx264_encoder_destroy(us_libx264_encoder_s *enc);

void us_libx264_encoder_init(us_libx264_encoder_s *enc, int frame_width, int frame_height){
    enc->param = (x264_param_t *)malloc(sizeof(x264_param_t));
    x264_param_default(enc->param);
    enc->param->i_threads = X264_SYNC_LOOKAHEAD_AUTO;//X264_SYNC_LOOKAHEAD_AUTO // 取空缓冲区继续使用不死锁的保证
    enc->param->i_width = frame_width; // 要编码的图像宽度.
    enc->param->i_height = frame_height; // 要编码的图像高度
    enc->param->i_fps_num = 30; // 帧率分子
    enc->param->i_fps_den = 1; // 帧率分母
    enc->param->i_csp = X264_CSP_I422;//X264_CSP_BGR,X264_CSP_I420
    enc->param->i_log_level = X264_LOG_INFO;//X264_LOG_DEBUG,X264_LOG_NONE
	//x264_param_apply_profile(enc->param, x264_profile_names[4]);
	printf("enc->param->i_width = %d;enc->param->i_height = %d\n",enc->param->i_width,enc->param->i_height);
	enc->handle = x264_encoder_open(enc->param);
    assert(enc->handle);
    //printf("x264_picture_allo\n");
    //x264_picture_alloc(enc->picture_in, enc->param->i_csp, enc->param->i_width, enc->param->i_height);
    /*x264_picture_t* pPic_in = (x264_picture_t*)malloc(sizeof(x264_picture_t));
    x264_picture_t* pPic_out = (x264_picture_t*)malloc(sizeof(x264_picture_t));
    x264_picture_alloc( pPic_in, enc->param->i_csp, enc->param->i_width, enc->param->i_height);
    printf("x264_picture_alloced!\n");*/
}

int us_libx264_encoder_compress(us_libx264_encoder_s *enc, const us_frame_s *src, us_frame_s *dest, bool force_key){
    x264_picture_t* pPic_in = (x264_picture_t*)malloc(sizeof(x264_picture_t));
    x264_picture_t* pPic_out = (x264_picture_t*)malloc(sizeof(x264_picture_t));
    x264_picture_alloc( pPic_in, enc->param->i_csp, enc->param->i_width, enc->param->i_height);


    int index_y=0;int index_u=0;int index_v = 0;
	int num = src->width * src->height * 2 - 4  ;
	int nNal = -1;
	int result = 0;
	//int i = 0;
	static long int pts = 0;
	uint8_t *p_out = dest->data;
	/*char *y = enc->picture_in->img.plane[0];   
	char *u = enc->picture_in->img.plane[1];   
	char *v = enc->picture_in->img.plane[2]; */
    uint8_t *y = pPic_in->img.plane[0];   
	uint8_t *u = pPic_in->img.plane[1];   
	uint8_t *v = pPic_in->img.plane[2];
	//printf("num = %d\n",num);
	for(int i=0; i<num+4; i=i+4) {
			*(y + (index_y++)) = *(src->data + i);
			*(u + (index_u++)) = *(src->data + i + 1);
			*(y + (index_y++)) = *(src->data + i + 2);
			*(v + (index_v++)) = *(src->data + i + 3);
	}
	pPic_in->i_type = X264_TYPE_AUTO;
	pPic_in->i_pts = pts++;
 
	if (x264_encoder_encode(enc->handle, &(enc->nal), &nNal, pPic_in,
			pPic_out) < 0) {
		return -1;
	}
    
	for (int i = 0; i < nNal; i++) {
		memcpy(p_out, enc->nal[i].p_payload, enc->nal[i].i_payload);   
		p_out += enc->nal[i].i_payload;								 
		result += enc->nal[i].i_payload;
	}
    US_FRAME_COPY_META(src, dest);
    /*int j = 0;
    FILE* fp_dst = fopen("test.libx264.mp4", "ab");
    for (j = 0; j < nNal; ++j){
        fwrite(enc->nal[j].p_payload, 1, enc->nal[j].i_payload, fp_dst);
    }
    fclose(fp_dst);*/
    free(pPic_in);
    free(pPic_out);
    return 1;
}

void us_libx264_encoder_destroy(us_libx264_encoder_s *enc){
}
