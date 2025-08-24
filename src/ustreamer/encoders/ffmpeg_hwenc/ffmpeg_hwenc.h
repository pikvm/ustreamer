#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>
#include <stdatomic.h>

#include "../../../libs/frame.h"

// FFmpeg硬件编码器类型
typedef enum {
	US_HWENC_NONE,
	US_HWENC_LIBX264,    // Software fallback via libx264
	US_HWENC_VAAPI,      // Intel Quick Sync Video / AMD VCE
	US_HWENC_AMF,        // AMD Advanced Media Framework
	US_HWENC_NVENC,      // NVIDIA NVENC
	US_HWENC_V4L2_M2M,   // Rockchip/AllWinner/其他SoC
	US_HWENC_RKMPP,      // Rockchip Media Process Platform
	US_HWENC_MEDIACODEC, // Android MediaCodec
	US_HWENC_VIDEOTOOLBOX // Apple VideoToolbox (macOS/iOS)
} us_hwenc_type_e;

// 硬件编码器错误码
typedef enum {
	US_HWENC_OK = 0,
	US_HWENC_ERROR_INVALID_PARAM = -1,
	US_HWENC_ERROR_MEMORY = -2,
	US_HWENC_ERROR_ENCODER_INIT = -3,
	US_HWENC_ERROR_ENCODE = -4,
	US_HWENC_ERROR_FORMAT_UNSUPPORTED = -5,
	US_HWENC_ERROR_DEVICE_NOT_FOUND = -6,
	US_HWENC_ERROR_DEVICE_BUSY = -7,
	US_HWENC_ERROR_HARDWARE_FAILURE = -8,
	US_HWENC_ERROR_NOT_INITIALIZED = -9,
	US_HWENC_ERROR_FFMPEG_ERROR = -10
} us_hwenc_error_e;

// 硬件编码器统计信息
typedef struct {
	uint64_t frames_encoded;
	uint64_t bytes_output;
	uint64_t encode_errors;
	double avg_encode_time_ms;
	double total_encode_time_ms;
	double current_fps;
	uint64_t last_stats_update;
} us_hwenc_stats_s;

// 前向声明（避免包含FFmpeg头文件）
struct AVCodecContext;
struct AVFrame;
struct AVPacket;
struct SwsContext;
struct AVBufferRef;

// FFmpeg硬件编码器结构体
typedef struct {
	// FFmpeg资源
	struct AVCodecContext *ctx;
	struct AVFrame *frame;
	struct AVPacket *pkt;
	struct SwsContext *sws_ctx;
	struct AVBufferRef *hw_device_ctx;
	struct AVBufferRef *hw_frames_ctx;
	
	// 编码器信息
	us_hwenc_type_e type;
	char codec_name[64];
	char device_path[256];
	
	// 配置参数
	int width;
	int height;
	uint bitrate_kbps;
	uint gop_size;
	int fps_num;
	int fps_den;
	
	// 状态管理
	atomic_bool initialized;
	atomic_bool encoding;
	
	// 错误处理
	us_hwenc_error_e last_error;
	char last_error_msg[256];
	uint32_t consecutive_errors;
	uint32_t max_consecutive_errors;
	
	// 统计信息
	us_hwenc_stats_s stats;
	
	// 编码参数
	char preset[32];
	char tune[32];
	char profile[32];
	
	// 帧计数器
	uint64_t frame_number;
	
	// 线程安全
	pthread_mutex_t mutex;
	bool mutex_initialized;
	
	// 性能统计
	uint64_t total_encode_time_us;
} us_ffmpeg_hwenc_s;

// 函数声明
us_hwenc_error_e us_ffmpeg_hwenc_create(us_ffmpeg_hwenc_s **encoder,
                                       us_hwenc_type_e type,
                                       int width, int height,
                                       uint bitrate_kbps, uint gop_size);

us_hwenc_error_e us_ffmpeg_hwenc_compress(us_ffmpeg_hwenc_s *encoder,
                                         const us_frame_s *src,
                                         us_frame_s *dest,
                                         bool force_key);

us_hwenc_error_e us_ffmpeg_hwenc_reset(us_ffmpeg_hwenc_s *encoder);

void us_ffmpeg_hwenc_destroy(us_ffmpeg_hwenc_s *encoder);

// 硬件编码器检测和管理
us_hwenc_type_e us_ffmpeg_hwenc_detect_best_encoder(void);
us_hwenc_error_e us_ffmpeg_hwenc_query_hardware_encoders(us_hwenc_type_e *types, uint32_t *count);
bool us_ffmpeg_hwenc_is_format_supported(us_hwenc_type_e encoder, uint32_t format);

// 辅助函数
const char* us_hwenc_error_string(us_hwenc_error_e error);
const char* us_hwenc_type_to_string(us_hwenc_type_e type);
const char* us_ffmpeg_hwenc_get_codec_name(us_hwenc_type_e type);
us_hwenc_error_e us_ffmpeg_hwenc_get_stats(us_ffmpeg_hwenc_s *encoder, us_hwenc_stats_s *stats);