#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <linux/videodev2.h>
#include <x264.h>
#include <libyuv.h>

#include "../../../libs/frame.h"
#include "../../../libs/logging.h"
#include "../../../libs/tools.h"

#include "libx264.h"

// 参数验证宏
#define US_H264_CHECK_PARAM(cond, error, msg, ...) \
	do { \
		if (!(cond)) { \
			US_LOG_ERROR("H264: " msg, ##__VA_ARGS__); \
			return error; \
		} \
	} while(0)

#define US_H264_CHECK_NOT_NULL(ptr, msg, ...) \
	US_H264_CHECK_PARAM((ptr) != NULL, US_H264_ERROR_INVALID_PARAM, \
	                    msg ": NULL pointer", ##__VA_ARGS__)

#define US_H264_CHECK_RANGE(val, min, max, msg, ...) \
	US_H264_CHECK_PARAM((val) >= (min) && (val) <= (max), \
	                    US_H264_ERROR_INVALID_PARAM, \
	                    msg ": %d not in range [%d, %d]", \
	                    (int)(val), (int)(min), (int)(max), ##__VA_ARGS__)

// 性能测量宏
#define US_H264_PERF_BEGIN(name) \
	uint64_t __perf_start_##name = us_get_now_monotonic_u64()

#define US_H264_PERF_END(encoder, name) \
	do { \
		uint64_t __perf_elapsed = us_get_now_monotonic_u64() - __perf_start_##name; \
		double elapsed_ms = (double)__perf_elapsed / 1000.0; \
		encoder->stats.avg_encode_time_ms = encoder->stats.avg_encode_time_ms * 0.9 + elapsed_ms * 0.1; \
	} while(0)

// 静态函数声明
static us_h264_error_e _us_libx264_encoder_init_internal(us_libx264_encoder_s *encoder);
static us_h264_error_e _us_libx264_encoder_setup_params(us_libx264_encoder_s *encoder);
static us_h264_error_e _us_libx264_encoder_convert_frame(const us_frame_s *src, us_libx264_encoder_s *encoder);
static void _us_libx264_encoder_update_stats(us_libx264_encoder_s *encoder, uint64_t bytes_output);
static void _us_libx264_encoder_set_error(us_libx264_encoder_s *encoder, us_h264_error_e error, const char *msg);

// 错误信息映射
const char* us_h264_error_string(us_h264_error_e error) {
	static const char* error_strings[] = {
		[US_H264_OK] = "Success",
		[-US_H264_ERROR_INVALID_PARAM] = "Invalid parameter",
		[-US_H264_ERROR_MEMORY] = "Memory allocation failed",
		[-US_H264_ERROR_ENCODER_INIT] = "Encoder initialization failed",
		[-US_H264_ERROR_ENCODE] = "Encoding failed",
		[-US_H264_ERROR_FORMAT_UNSUPPORTED] = "Unsupported format",
		[-US_H264_ERROR_TIMEOUT] = "Operation timeout",
		[-US_H264_ERROR_DEVICE_BUSY] = "Device busy",
		[-US_H264_ERROR_HARDWARE_FAILURE] = "Hardware failure",
		[-US_H264_ERROR_NOT_INITIALIZED] = "Encoder not initialized"
	};
	
	if (error > 0 || -error >= (int)(sizeof(error_strings) / sizeof(error_strings[0]))) {
		return "Unknown error";
	}
	
	return error_strings[-error];
}

// 验证预设有效性
bool us_libx264_is_valid_preset(const char *preset) {
	if (!preset) return false;
	
	static const char *valid_presets[] = {
		"ultrafast", "superfast", "veryfast", 
		"faster", "fast", "medium", 
		"slow", "slower", "veryslow", "placebo"
	};
	
	for (size_t i = 0; i < sizeof(valid_presets) / sizeof(valid_presets[0]); i++) {
		if (!strcmp(preset, valid_presets[i])) {
			return true;
		}
	}
	return false;
}

// 向后兼容函数（重命名旧函数）
bool is_valid_preset(const char* preset) {
	return us_libx264_is_valid_preset(preset);
}

// 设置错误信息
static void _us_libx264_encoder_set_error(us_libx264_encoder_s *encoder, us_h264_error_e error, const char *msg) {
	if (!encoder) return;
	
	encoder->last_error = error;
	if (msg) {
		strncpy(encoder->last_error_msg, msg, sizeof(encoder->last_error_msg) - 1);
		encoder->last_error_msg[sizeof(encoder->last_error_msg) - 1] = '\0';
	} else {
		strncpy(encoder->last_error_msg, us_h264_error_string(error), sizeof(encoder->last_error_msg) - 1);
		encoder->last_error_msg[sizeof(encoder->last_error_msg) - 1] = '\0';
	}
}

// 更新统计信息
static void _us_libx264_encoder_update_stats(us_libx264_encoder_s *encoder, uint64_t bytes_output) {
	if (!encoder) return;
	
	encoder->stats.frames_encoded++;
	encoder->stats.bytes_output += bytes_output;
	
	// 计算当前FPS
	uint64_t now = us_get_now_monotonic_u64();
	if (encoder->stats.last_stats_update > 0) {
		double time_diff_s = (now - encoder->stats.last_stats_update) / 1000000.0;
		if (time_diff_s > 1.0) { // 每秒更新一次FPS
			encoder->stats.current_fps = 1.0 / time_diff_s;
			encoder->stats.last_stats_update = now;
		}
	} else {
		encoder->stats.last_stats_update = now;
	}
}

// 创建编码器
us_h264_error_e us_libx264_encoder_create(us_libx264_encoder_s **encoder, 
                                          int width, int height, 
                                          uint bitrate_kbps, uint gop_size, 
                                          const char *preset) {
	US_H264_CHECK_NOT_NULL(encoder, "Encoder pointer");
	US_H264_CHECK_RANGE(width, 64, 7680, "Width");
	US_H264_CHECK_RANGE(height, 64, 4320, "Height");
	US_H264_CHECK_PARAM(width % 2 == 0, US_H264_ERROR_INVALID_PARAM, "Width must be even: %d", width);
	US_H264_CHECK_PARAM(height % 2 == 0, US_H264_ERROR_INVALID_PARAM, "Height must be even: %d", height);
	US_H264_CHECK_RANGE(bitrate_kbps, 64, 100000, "Bitrate");
	US_H264_CHECK_RANGE(gop_size, 1, 1000, "GOP size");
	
	// 验证预设
	if (preset && !us_libx264_is_valid_preset(preset)) {
		US_LOG_ERROR("H264: Invalid preset: %s", preset);
		return US_H264_ERROR_INVALID_PARAM;
	}
	
	// 分配编码器结构体
	us_libx264_encoder_s *enc = calloc(1, sizeof(*enc));
	if (!enc) {
		US_LOG_ERROR("H264: Failed to allocate encoder structure");
		return US_H264_ERROR_MEMORY;
	}
	
	// 初始化基本参数
	enc->width = width;
	enc->height = height;
	enc->bitrate_kbps = bitrate_kbps;
	enc->gop_size = gop_size;
	enc->max_consecutive_errors = 10;
	
	// 设置预设
	if (preset) {
		strncpy(enc->preset, preset, sizeof(enc->preset) - 1);
	} else {
		strncpy(enc->preset, "ultrafast", sizeof(enc->preset) - 1);
	}
	enc->preset[sizeof(enc->preset) - 1] = '\0';
	
	// 初始化原子变量
	atomic_init(&enc->initialized, false);
	atomic_init(&enc->encoding, false);
	
	// 初始化互斥锁
	if (pthread_mutex_init(&enc->mutex, NULL) != 0) {
		US_LOG_ERROR("H264: Failed to initialize mutex");
		free(enc);
		return US_H264_ERROR_ENCODER_INIT;
	}
	enc->mutex_initialized = true;
	
	// 初始化编码器
	us_h264_error_e result = _us_libx264_encoder_init_internal(enc);
	if (result != US_H264_OK) {
		us_libx264_encoder_destroy(enc);
		return result;
	}
	
	atomic_store(&enc->initialized, true);
	*encoder = enc;
	
	US_LOG_INFO("H264: Encoder created successfully (%dx%d @ %u kbps, preset: %s)",
	           width, height, bitrate_kbps, enc->preset);
	return US_H264_OK;
}

// 内部初始化函数
static us_h264_error_e _us_libx264_encoder_init_internal(us_libx264_encoder_s *encoder) {
	if (!encoder) return US_H264_ERROR_INVALID_PARAM;
	
	// 分配x264参数结构体
	encoder->param = calloc(1, sizeof(x264_param_t));
	if (!encoder->param) {
		US_LOG_ERROR("H264: Failed to allocate x264 parameters");
		return US_H264_ERROR_MEMORY;
	}
	
	// 设置默认预设
	if (x264_param_default_preset(encoder->param, encoder->preset, NULL) < 0) {
		US_LOG_ERROR("H264: Failed to set preset: %s", encoder->preset);
		return US_H264_ERROR_ENCODER_INIT;
	}
	
	return _us_libx264_encoder_setup_params(encoder);
}

// 设置编码器参数
static us_h264_error_e _us_libx264_encoder_setup_params(us_libx264_encoder_s *encoder) {
	if (!encoder || !encoder->param) return US_H264_ERROR_INVALID_PARAM;
	
	x264_param_t *param = encoder->param;
	
	// 基本参数
	param->i_width = encoder->width;
	param->i_height = encoder->height;
	param->i_fps_num = (encoder->width <= 1280 && encoder->height <= 720) ? 60 : 30;
	param->i_fps_den = 1;
	
	// 日志设置
	param->i_log_level = X264_LOG_NONE;
	
	// 比特率控制
	param->rc.i_rc_method = X264_RC_ABR;
	param->rc.i_bitrate = encoder->bitrate_kbps;
	param->rc.i_vbv_max_bitrate = encoder->bitrate_kbps;
	param->rc.i_vbv_buffer_size = encoder->bitrate_kbps / 2;
	param->rc.b_mb_tree = 0;
	
	// GOP设置
	param->i_keyint_max = encoder->gop_size;
	
	// 头部重复
	param->b_repeat_headers = 1;
	
	// 线程设置
	param->i_threads = sysconf(_SC_NPROCESSORS_ONLN);
	param->b_sliced_threads = 0;
	
	// 分配图像结构体
	encoder->picture_in = calloc(1, sizeof(x264_picture_t));
	if (!encoder->picture_in) {
		US_LOG_ERROR("H264: Failed to allocate picture structure");
		return US_H264_ERROR_MEMORY;
	}
	
	return US_H264_OK;
}

// 获取统计信息
us_h264_error_e us_libx264_encoder_get_stats(us_libx264_encoder_s *encoder, us_h264_stats_s *stats) {
	US_H264_CHECK_NOT_NULL(encoder, "Encoder");
	US_H264_CHECK_NOT_NULL(stats, "Stats");
	
	if (!atomic_load(&encoder->initialized)) {
		return US_H264_ERROR_NOT_INITIALIZED;
	}
	
	pthread_mutex_lock(&encoder->mutex);
	memcpy(stats, &encoder->stats, sizeof(*stats));
	pthread_mutex_unlock(&encoder->mutex);
	
	return US_H264_OK;
}

// 重置编码器
us_h264_error_e us_libx264_encoder_reset(us_libx264_encoder_s *encoder) {
	US_H264_CHECK_NOT_NULL(encoder, "Encoder");
	
	if (!atomic_load(&encoder->initialized)) {
		return US_H264_ERROR_NOT_INITIALIZED;
	}
	
	pthread_mutex_lock(&encoder->mutex);
	
	// 等待当前编码完成
	while (atomic_load(&encoder->encoding)) {
		pthread_mutex_unlock(&encoder->mutex);
		usleep(1000); // 1ms
		pthread_mutex_lock(&encoder->mutex);
	}
	
	US_LOG_INFO("H264: Resetting encoder");
	
	// 关闭当前编码器
	if (encoder->handle) {
		x264_encoder_close(encoder->handle);
		encoder->handle = NULL;
	}
	
	if (encoder->picture_in) {
		x264_picture_clean(encoder->picture_in);
		free(encoder->picture_in);
		encoder->picture_in = NULL;
	}
	
	// 重新初始化
	us_h264_error_e result = _us_libx264_encoder_init_internal(encoder);
	if (result != US_H264_OK) {
		_us_libx264_encoder_set_error(encoder, result, "Failed to reinitialize encoder");
	} else {
		encoder->consecutive_errors = 0;
	}
	
	pthread_mutex_unlock(&encoder->mutex);
	return result;
}

// 格式转换函数
static us_h264_error_e _us_libx264_encoder_convert_frame(const us_frame_s *src, us_libx264_encoder_s *encoder) {
	if (!src || !encoder) return US_H264_ERROR_INVALID_PARAM;
	
	switch (src->format) {
		case V4L2_PIX_FMT_YUYV:
			// TODO: 实现YUYV转换
			US_LOG_ERROR("H264: YUYV format conversion not implemented yet");
			return US_H264_ERROR_FORMAT_UNSUPPORTED;
			
		case V4L2_PIX_FMT_YUV420:
			encoder->picture_in->img.plane[0] = src->data;
			encoder->picture_in->img.plane[1] = src->data + src->width * src->height;
			encoder->picture_in->img.plane[2] = src->data + src->width * src->height * 5 / 4;
			break;
			
		case V4L2_PIX_FMT_RGB24:
			if (RGB24ToI420(src->data, src->width * 3, 
			               encoder->picture_in->img.plane[0], src->width,
			               encoder->picture_in->img.plane[2], src->width / 2, 
			               encoder->picture_in->img.plane[1], src->width / 2, 
			               src->width, src->height)) {
				US_LOG_ERROR("H264: RGB24ToI420 conversion failed");
				return US_H264_ERROR_ENCODE;
			}
			break;
			
		default:
			US_LOG_ERROR("H264: Unsupported input format: %d", src->format);
			return US_H264_ERROR_FORMAT_UNSUPPORTED;
	}
	
	return US_H264_OK;
}

// 新的编码函数
us_h264_error_e us_libx264_encoder_compress(us_libx264_encoder_s *encoder,
                                            const us_frame_s *src,
                                            us_frame_s *dest, 
                                            bool force_key) {
	US_H264_CHECK_NOT_NULL(encoder, "Encoder");
	US_H264_CHECK_NOT_NULL(src, "Source frame");
	US_H264_CHECK_NOT_NULL(dest, "Destination frame");
	
	if (!atomic_load(&encoder->initialized)) {
		return US_H264_ERROR_NOT_INITIALIZED;
	}
	
	pthread_mutex_lock(&encoder->mutex);
	atomic_store(&encoder->encoding, true);
	
	US_H264_PERF_BEGIN(encode);
	us_h264_error_e result = US_H264_OK;
	
	// 初始化编码器（如果需要）
	if (!encoder->handle) {
		// 根据输入格式设置CSP
		switch (src->format) {
			case V4L2_PIX_FMT_YUYV:
				encoder->param->i_csp = X264_CSP_I422;
				US_LOG_INFO("H264: Input format is YUYV, using CSP I422");
				break;
			case V4L2_PIX_FMT_YUV420:
				encoder->param->i_csp = X264_CSP_I420;
				US_LOG_INFO("H264: Input format is YUV420, using CSP I420");
				break;
			case V4L2_PIX_FMT_RGB24:
				encoder->param->i_csp = X264_CSP_I420;
				US_LOG_INFO("H264: Input format is RGB24, converting to I420");
				break;
			default:
				result = US_H264_ERROR_FORMAT_UNSUPPORTED;
				goto cleanup;
		}
		
		// 分配图像缓冲区
		if (x264_picture_alloc(encoder->picture_in, encoder->param->i_csp, 
		                      encoder->param->i_width, encoder->param->i_height) < 0) {
			US_LOG_ERROR("H264: Failed to allocate picture buffer");
			result = US_H264_ERROR_MEMORY;
			goto cleanup;
		}
		
		// 打开编码器
		encoder->handle = x264_encoder_open(encoder->param);
		if (!encoder->handle) {
			US_LOG_ERROR("H264: Failed to open encoder");
			result = US_H264_ERROR_ENCODER_INIT;
			goto cleanup;
		}
		
		US_LOG_INFO("H264: Encoder opened successfully");
	}
	
	// 转换帧格式
	result = _us_libx264_encoder_convert_frame(src, encoder);
	if (result != US_H264_OK) {
		goto cleanup;
	}
	
	// 设置时间戳和关键帧
	static long int pts = 0;
	encoder->picture_in->i_pts = pts++;
	encoder->picture_in->i_type = force_key ? X264_TYPE_KEYFRAME : X264_TYPE_AUTO;
	
	// 执行编码
	x264_picture_t picture_out;
	int nNal = 0;
	int encode_result = x264_encoder_encode(encoder->handle, &encoder->nal, &nNal, 
	                                       encoder->picture_in, &picture_out);
	if (encode_result < 0) {
		US_LOG_ERROR("H264: Encoding failed");
		result = US_H264_ERROR_ENCODE;
		encoder->stats.encode_errors++;
		encoder->consecutive_errors++;
		goto cleanup;
	}
	
	// 复制编码数据
	uint8_t *p_out = dest->data;
	size_t total_size = 0;
	
	for (int i = 0; i < nNal; i++) {
		if (total_size + encoder->nal[i].i_payload > dest->allocated) {
			US_LOG_ERROR("H264: Output buffer too small");
			result = US_H264_ERROR_MEMORY;
			goto cleanup;
		}
		
		memcpy(p_out, encoder->nal[i].p_payload, encoder->nal[i].i_payload);   
		p_out += encoder->nal[i].i_payload;
		total_size += encoder->nal[i].i_payload;
	}
	
	// 设置输出帧元数据
	US_FRAME_COPY_META(src, dest);
	dest->format = V4L2_PIX_FMT_H264;
	dest->used = total_size;
	dest->key = (picture_out.i_type == X264_TYPE_IDR || picture_out.i_type == X264_TYPE_I);
	
	// 更新统计信息
	_us_libx264_encoder_update_stats(encoder, total_size);
	encoder->consecutive_errors = 0; // 重置错误计数
	
cleanup:
	US_H264_PERF_END(encoder, encode);
	
	// 如果连续错误过多，考虑重置编码器
	if (result != US_H264_OK) {
		_us_libx264_encoder_set_error(encoder, result, NULL);
		if (encoder->consecutive_errors >= encoder->max_consecutive_errors) {
			US_LOG_ERROR("H264: Too many consecutive errors (%u), will reset encoder on next call",
			           encoder->consecutive_errors);
		}
	}
	
	atomic_store(&encoder->encoding, false);
	pthread_mutex_unlock(&encoder->mutex);
	
	return result;
}

// 向后兼容的旧初始化函数
void us_libx264_encoder_init(us_libx264_encoder_s *enc, int frame_width, int frame_height, uint h264_bitrate, uint h264_gop, char *h264_preset) {
	US_LOG_INFO("H264: Using deprecated us_libx264_encoder_init function. Please use us_libx264_encoder_create instead.");
	
	// 清零结构体
	memset(enc, 0, sizeof(*enc));
	
	// 设置基本参数
	enc->width = frame_width;
	enc->height = frame_height;
	enc->bitrate_kbps = h264_bitrate;
	enc->gop_size = h264_gop;
	
	// 设置预设
	if (h264_preset && is_valid_preset(h264_preset)) {
		strncpy(enc->preset, h264_preset, sizeof(enc->preset) - 1);
		US_LOG_INFO("H264 Encoder libx264: Preset: %s", h264_preset);
	} else {
		strncpy(enc->preset, "ultrafast", sizeof(enc->preset) - 1);
		US_LOG_INFO("H264 Encoder libx264: Invalid preset, using default preset: ultrafast");
	}
	enc->preset[sizeof(enc->preset) - 1] = '\0';
	
	enc->max_consecutive_errors = 10;
	atomic_init(&enc->initialized, false);
	atomic_init(&enc->encoding, false);
	
	// 初始化互斥锁
	if (pthread_mutex_init(&enc->mutex, NULL) == 0) {
		enc->mutex_initialized = true;
	}
	
	// 执行内部初始化
	if (_us_libx264_encoder_init_internal(enc) == US_H264_OK) {
		atomic_store(&enc->initialized, true);
		US_LOG_INFO("H264 Encoder libx264: Initialized successfully");
	} else {
		US_LOG_ERROR("H264 Encoder libx264: Initialization failed");
	}
}

// 向后兼容的旧编码函数
int us_libx264_encoder_compress_legacy(us_libx264_encoder_s *enc, const us_frame_s *src, us_frame_s *dest, bool force_key) {
	US_LOG_VERBOSE("H264: Using deprecated us_libx264_encoder_compress_legacy function");
	
	// 调用新的编码函数
	us_h264_error_e result = us_libx264_encoder_compress(enc, src, dest, force_key);
	return (result == US_H264_OK) ? 0 : -1;
}

// 增强的销毁函数
void us_libx264_encoder_destroy(us_libx264_encoder_s *encoder) {
	if (!encoder) return;
	
	US_LOG_DEBUG("H264: Destroying encoder");
	
	// 等待当前编码完成
	if (atomic_load(&encoder->encoding)) {
		US_LOG_INFO("H264: Waiting for current encoding to complete...");
		while (atomic_load(&encoder->encoding)) {
			usleep(1000); // 1ms
		}
	}
	
	// 加锁保护资源清理
	if (encoder->mutex_initialized) {
		pthread_mutex_lock(&encoder->mutex);
	}
	
	// 清理x264资源
	if (encoder->handle) {
		x264_encoder_close(encoder->handle);
		encoder->handle = NULL;
	}
	
	if (encoder->picture_in) {
		x264_picture_clean(encoder->picture_in);
		free(encoder->picture_in);
		encoder->picture_in = NULL;
	}
	
	if (encoder->param) {
		free(encoder->param);
		encoder->param = NULL;
	}
	
	atomic_store(&encoder->initialized, false);
	
	// 输出最终统计信息
	if (encoder->stats.frames_encoded > 0) {
		double compression_ratio = encoder->stats.frames_encoded > 0 ? 
			(double)encoder->stats.bytes_output / (encoder->width * encoder->height * encoder->stats.frames_encoded * 1.5) : 0.0;
		
		US_LOG_INFO("H264: Final stats - Frames: %lu, Output: %lu bytes, Errors: %lu, Avg time: %.2fms, Compression: %.2f",
		           encoder->stats.frames_encoded, encoder->stats.bytes_output, 
		           encoder->stats.encode_errors, encoder->stats.avg_encode_time_ms, compression_ratio);
	}
	
	if (encoder->mutex_initialized) {
		pthread_mutex_unlock(&encoder->mutex);
		pthread_mutex_destroy(&encoder->mutex);
		encoder->mutex_initialized = false;
	}
	
	// 释放编码器结构体本身
	free(encoder);
	US_LOG_DEBUG("H264: Encoder destroyed");
}

// 向后兼容的旧销毁函数
void us_libx264_encoder_destroy_legacy(us_libx264_encoder_s *enc) {
	if (!enc) return;
	
	// 只清理libx264资源，不释放结构体本身
	if (enc->handle) {
		x264_encoder_close(enc->handle);
		enc->handle = NULL;
	}
	
	if (enc->picture_in) {
		x264_picture_clean(enc->picture_in);
		free(enc->picture_in);
		enc->picture_in = NULL;
	}
	
	if (enc->param) {
		free(enc->param);
		enc->param = NULL;
	}
	
	if (enc->mutex_initialized) {
		pthread_mutex_destroy(&enc->mutex);
		enc->mutex_initialized = false;
	}
	
	atomic_store(&enc->initialized, false);
}