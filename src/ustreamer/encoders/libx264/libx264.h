#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>
#include <stdatomic.h>
#include <x264.h>

#include "../../../libs/frame.h"


// H.264编码器错误码定义
typedef enum {
	US_H264_OK = 0,
	US_H264_ERROR_INVALID_PARAM = -1,
	US_H264_ERROR_MEMORY = -2,
	US_H264_ERROR_ENCODER_INIT = -3,
	US_H264_ERROR_ENCODE = -4,
	US_H264_ERROR_FORMAT_UNSUPPORTED = -5,
	US_H264_ERROR_TIMEOUT = -6,
	US_H264_ERROR_DEVICE_BUSY = -7,
	US_H264_ERROR_HARDWARE_FAILURE = -8,
	US_H264_ERROR_NOT_INITIALIZED = -9
} us_h264_error_e;

// 编码统计信息
typedef struct {
	uint64_t frames_encoded;
	uint64_t bytes_output;
	uint64_t encode_errors;
	double avg_encode_time_ms;
	double current_fps;
	uint64_t last_stats_update;
} us_h264_stats_s;

// 增强的编码器结构体
typedef struct {
	// libx264资源
	x264_param_t *param;
	x264_t *handle;
	x264_picture_t *picture_in;
	x264_nal_t *nal;
	
	// 配置参数
	int width;
	int height;
	uint bitrate_kbps;
	uint gop_size;
	char preset[16];
	
	// 状态管理
	atomic_bool initialized;
	atomic_bool encoding;
	
	// 错误处理
	us_h264_error_e last_error;
	char last_error_msg[256];
	uint32_t consecutive_errors;
	uint32_t max_consecutive_errors;
	
	// 统计信息
	us_h264_stats_s stats;
	
	// 线程安全
	pthread_mutex_t mutex;
	bool mutex_initialized;
} us_libx264_encoder_s;

// 函数声明
us_h264_error_e us_libx264_encoder_create(us_libx264_encoder_s **encoder, 
                                          int width, int height, 
                                          uint bitrate_kbps, uint gop_size, 
                                          const char *preset);
                                          
us_h264_error_e us_libx264_encoder_compress(us_libx264_encoder_s *encoder,
                                            const us_frame_s *src,
                                            us_frame_s *dest, 
                                            bool force_key);
                                            
us_h264_error_e us_libx264_encoder_reset(us_libx264_encoder_s *encoder);

void us_libx264_encoder_destroy(us_libx264_encoder_s *encoder);

// 辅助函数
const char* us_h264_error_string(us_h264_error_e error);
bool us_libx264_is_valid_preset(const char *preset);
us_h264_error_e us_libx264_encoder_get_stats(us_libx264_encoder_s *encoder, us_h264_stats_s *stats);

// 向后兼容的旧接口（已废弃）
void us_libx264_encoder_init(us_libx264_encoder_s *enc, int frame_width, int frame_height, uint h264_bitrate, uint h264_gop, char *h264_preset) __attribute__((deprecated));
int us_libx264_encoder_compress_legacy(us_libx264_encoder_s *enc, const us_frame_s *src, us_frame_s *dest, bool force_key) __attribute__((deprecated));
void us_libx264_encoder_destroy_legacy(us_libx264_encoder_s *enc) __attribute__((deprecated));