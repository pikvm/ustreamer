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

// 编码档案枚举
typedef enum {
	US_H264_PROFILE_REALTIME,    // 实时流：ultrafast
	US_H264_PROFILE_BALANCED,    // 平衡：veryfast/faster
	US_H264_PROFILE_QUALITY,     // 高质量：medium/slow
	US_H264_PROFILE_ARCHIVE      // 存档：slow/slower
} us_h264_profile_e;

// H.264调优选项
typedef enum {
	US_H264_TUNE_NONE,
	US_H264_TUNE_FILM,
	US_H264_TUNE_ANIMATION,
	US_H264_TUNE_GRAIN,
	US_H264_TUNE_STILLIMAGE,
	US_H264_TUNE_PSNR,
	US_H264_TUNE_SSIM,
	US_H264_TUNE_FASTDECODE,
	US_H264_TUNE_ZEROLATENCY
} us_h264_tune_e;

// 自适应质量控制结构
typedef struct {
	us_h264_profile_e current_profile;
	double target_encode_time_ms;    // 目标编码时间
	double avg_encode_time_ms;       // 平均编码时间
	uint32_t adaptation_counter;     // 适配计数器
	bool adaptation_enabled;         // 是否启用自适应
	uint64_t last_adaptation_time;   // 上次调整时间
} us_adaptive_quality_s;

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
	us_h264_profile_e profile;
	us_h264_tune_e tune;
	int fps_num;
	int fps_den;
	
	// 智能预设相关
	us_adaptive_quality_s adaptive_quality;
	bool auto_preset_enabled;
	
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

// 智能预设相关函数
const char* us_libx264_select_optimal_preset(int width, int height, int fps, int bitrate_kbps);
const char* us_libx264_get_preset_by_profile(us_h264_profile_e profile, int width, int height);
int us_libx264_get_optimal_threads(int width, int height);
us_h264_profile_e us_libx264_determine_profile_by_usage(int width, int height, int fps, int bitrate_kbps);
us_h264_error_e us_libx264_encoder_enable_adaptive_quality(us_libx264_encoder_s *encoder, double target_fps);
us_h264_error_e us_libx264_encoder_update_adaptive_quality(us_libx264_encoder_s *encoder);
const char* us_libx264_profile_to_string(us_h264_profile_e profile);
const char* us_libx264_tune_to_string(us_h264_tune_e tune);

// 向后兼容的旧接口（已废弃）
void us_libx264_encoder_init(us_libx264_encoder_s *enc, int frame_width, int frame_height, uint h264_bitrate, uint h264_gop, char *h264_preset) __attribute__((deprecated));
int us_libx264_encoder_compress_legacy(us_libx264_encoder_s *enc, const us_frame_s *src, us_frame_s *dest, bool force_key) __attribute__((deprecated));
void us_libx264_encoder_destroy_legacy(us_libx264_encoder_s *enc) __attribute__((deprecated));