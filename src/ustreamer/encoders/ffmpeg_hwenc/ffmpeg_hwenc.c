#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <linux/videodev2.h>

#ifdef WITH_FFMPEG
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/hwcontext.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#ifdef WITH_VAAPI
#include <libavutil/hwcontext_vaapi.h>
#endif
#endif

#include "../../../libs/frame.h"
#include "../../../libs/logging.h"
#include "../../../libs/tools.h"

#include "ffmpeg_hwenc.h"

// CPU核心数检测相关头文件
#ifdef __linux__
#include <unistd.h>
#endif

// 参数验证宏
#define US_HWENC_CHECK_PARAM(cond, error, msg, ...) \
	do { \
		if (!(cond)) { \
			US_LOG_ERROR("HWENC: " msg, ##__VA_ARGS__); \
			return error; \
		} \
	} while(0)

#define US_HWENC_CHECK_NOT_NULL(ptr, msg, ...) \
	US_HWENC_CHECK_PARAM((ptr) != NULL, US_HWENC_ERROR_INVALID_PARAM, \
	                     msg ": NULL pointer", ##__VA_ARGS__)

// 错误信息映射
const char* us_hwenc_error_string(us_hwenc_error_e error) {
	static const char* error_strings[] = {
		[US_HWENC_OK] = "Success",
		[-US_HWENC_ERROR_INVALID_PARAM] = "Invalid parameter",
		[-US_HWENC_ERROR_MEMORY] = "Memory allocation failed",
		[-US_HWENC_ERROR_ENCODER_INIT] = "Hardware encoder initialization failed",
		[-US_HWENC_ERROR_ENCODE] = "Hardware encoding failed",
		[-US_HWENC_ERROR_FORMAT_UNSUPPORTED] = "Unsupported format",
		[-US_HWENC_ERROR_DEVICE_NOT_FOUND] = "Hardware device not found",
		[-US_HWENC_ERROR_DEVICE_BUSY] = "Hardware device busy",
		[-US_HWENC_ERROR_HARDWARE_FAILURE] = "Hardware failure",
		[-US_HWENC_ERROR_NOT_INITIALIZED] = "Encoder not initialized",
		[-US_HWENC_ERROR_FFMPEG_ERROR] = "FFmpeg error"
	};
	
	if (error > 0 || -error >= (int)(sizeof(error_strings) / sizeof(error_strings[0]))) {
		return "Unknown error";
	}
	
	return error_strings[-error];
}

// 编码器类型转字符串
const char* us_hwenc_type_to_string(us_hwenc_type_e type) {
	switch (type) {
		case US_HWENC_LIBX264: return "libx264";
		case US_HWENC_VAAPI: return "vaapi";
		case US_HWENC_NVENC: return "nvenc";
		case US_HWENC_AMF: return "amf";
		case US_HWENC_V4L2_M2M: return "v4l2m2m";
		case US_HWENC_RKMPP: return "rkmpp";
		case US_HWENC_MEDIACODEC: return "mediacodec";
		case US_HWENC_VIDEOTOOLBOX: return "videotoolbox";
		default: return "unknown";
	}
}

#ifdef WITH_FFMPEG

// 获取CPU核心数
static int _get_cpu_core_count() {
	int cpu_count = 1;
	
#ifdef __linux__
	cpu_count = (int)sysconf(_SC_NPROCESSORS_ONLN);  // 获取在线 CPU 核心数
	if (cpu_count <= 0) {
		cpu_count = 1;
	}
#elif defined(_WIN32)
	SYSTEM_INFO sysinfo;
	GetSystemInfo(&sysinfo);
	cpu_count = sysinfo.dwNumberOfProcessors;
#else
	// 其他平台默认为1核心
	cpu_count = 1;
#endif
	
	return cpu_count;
}

// 根据preset和CPU核心数获取最佳线程数
static int _get_optimal_threads_by_preset(const char *preset, int width, int height) {
	int cpu_cores = _get_cpu_core_count();
	int optimal_threads;
	
	// 所有设备都充分利用CPU核心，但有上限
	// 基本原则：线程数 = CPU核心数，但不超过合理上限
	optimal_threads = cpu_cores;
	
	// 根据preset调整线程策略
	if (!preset || strcmp(preset, "ultrafast") == 0) {
		// ultrafast: 使用所有核心，但限制在合理范围内
		optimal_threads = cpu_cores;
	} else if (strcmp(preset, "veryfast") == 0 || strcmp(preset, "faster") == 0) {
		// veryfast/faster: 可以使用更多线程因为编码复杂度中等
		optimal_threads = cpu_cores;
	} else if (strcmp(preset, "fast") == 0 || strcmp(preset, "medium") == 0) {
		// fast/medium: 编码复杂度高，可以充分利用所有核心
		optimal_threads = cpu_cores;
	} else {
		// slow/slower/veryslow: 最复杂编码，可以使用所有核心
		optimal_threads = cpu_cores;
	}
	
	// 根据分辨率进行微调
	int pixel_count = width * height;
	if (pixel_count > 1920 * 1080) {
		// 4K及以上分辨率，可以适当增加线程数
		optimal_threads = cpu_cores + 2;
	} else if (pixel_count < 320 * 240) {
		// 非常低的分辨率，可以适当减少线程数
		optimal_threads = (cpu_cores > 2) ? cpu_cores / 2 : cpu_cores;
	}
	
	// 设置合理的上限和下限
	if (optimal_threads < 1) optimal_threads = 1;
	if (optimal_threads > 16) optimal_threads = 16;  // 防止过多线程导致开销
	
	return optimal_threads;
}

// 获取硬件设备类型
static const char* _get_hw_device_type(us_hwenc_type_e type) {
	switch (type) {
		case US_HWENC_VAAPI: return "vaapi";
		case US_HWENC_NVENC: return "cuda";
		case US_HWENC_AMF: return "d3d11va";
		case US_HWENC_RKMPP: return "rkmpp";
		case US_HWENC_VIDEOTOOLBOX: return "videotoolbox";
		default: return NULL;
	}
}

// 获取编码器名称
const char* us_ffmpeg_hwenc_get_codec_name(us_hwenc_type_e type) {
	switch (type) {
		case US_HWENC_LIBX264: return "libx264";
		case US_HWENC_VAAPI: return "h264_vaapi";
		case US_HWENC_NVENC: return "h264_nvenc";
		case US_HWENC_AMF: return "h264_amf";
		case US_HWENC_V4L2_M2M: return "h264_v4l2m2m";
		case US_HWENC_RKMPP: return "h264_rkmpp";
		case US_HWENC_MEDIACODEC: return "h264_mediacodec";
		case US_HWENC_VIDEOTOOLBOX: return "h264_videotoolbox";
		default: return "";
	}
}

// 创建编码器的核心实现
us_hwenc_error_e us_ffmpeg_hwenc_create(us_ffmpeg_hwenc_s **encoder,
                                       us_hwenc_type_e type,
                                       int width, int height,
                                       uint bitrate_kbps, uint gop_size) {
	if (!encoder) {
		US_LOG_ERROR("HWENC: Encoder pointer is NULL");
		return US_HWENC_ERROR_INVALID_PARAM;
	}
	
	*encoder = NULL;

	// 分配编码器结构体
	us_ffmpeg_hwenc_s *enc = calloc(1, sizeof(us_ffmpeg_hwenc_s));
	if (!enc) {
		US_LOG_ERROR("HWENC: Failed to allocate encoder structure");
		return US_HWENC_ERROR_MEMORY;
	}

	// 初始化互斥锁
	if (pthread_mutex_init(&enc->mutex, NULL) != 0) {
		US_LOG_ERROR("HWENC: Failed to initialize mutex");
		free(enc);
		return US_HWENC_ERROR_MEMORY;
	}
	enc->mutex_initialized = true;

	// 设置基础参数
	enc->type = type;
	enc->width = width;
	enc->height = height;
	enc->bitrate_kbps = bitrate_kbps;
	enc->gop_size = gop_size;
	strncpy(enc->codec_name, us_ffmpeg_hwenc_get_codec_name(type), sizeof(enc->codec_name) - 1);

	// 查找编码器
	const AVCodec *codec = avcodec_find_encoder_by_name(enc->codec_name);
	if (!codec) {
		US_LOG_ERROR("HWENC: Codec %s not found", enc->codec_name);
		us_ffmpeg_hwenc_destroy(enc);
		return US_HWENC_ERROR_ENCODER_INIT;
	}

	// 创建编码器上下文
	enc->ctx = avcodec_alloc_context3(codec);
	if (!enc->ctx) {
		US_LOG_ERROR("HWENC: Failed to allocate codec context");
		us_ffmpeg_hwenc_destroy(enc);
		return US_HWENC_ERROR_MEMORY;
	}

	// 配置编码器参数
	enc->ctx->width = width;
	enc->ctx->height = height;
	enc->ctx->time_base = (AVRational){1, 25};
	enc->ctx->framerate = (AVRational){25, 1};
	enc->ctx->bit_rate = bitrate_kbps * 1000;
	enc->ctx->gop_size = gop_size;
	enc->ctx->max_b_frames = 0;
	enc->ctx->pix_fmt = AV_PIX_FMT_YUV420P;

	// 硬件设备初始化
	const char *hw_device_type = _get_hw_device_type(type);
	if (hw_device_type) {
		enum AVHWDeviceType device_type = av_hwdevice_find_type_by_name(hw_device_type);
		if (device_type == AV_HWDEVICE_TYPE_NONE) {
			US_LOG_ERROR("HWENC: Hardware device type %s not found", hw_device_type);
			us_ffmpeg_hwenc_destroy(enc);
			return US_HWENC_ERROR_DEVICE_NOT_FOUND;
		}

		// 尝试创建硬件设备上下文，对VAAPI使用指定设备路径
		const char *device_name = NULL;
		if (type == US_HWENC_VAAPI) {
			device_name = "/dev/dri/renderD128";  // 指定VAAPI设备
		}
		
		int ret = av_hwdevice_ctx_create(&enc->hw_device_ctx, device_type, device_name, NULL, 0);
		if (ret < 0) {
			char errbuf[AV_ERROR_MAX_STRING_SIZE];
			av_strerror(ret, errbuf, sizeof(errbuf));
			US_LOG_ERROR("HWENC: Failed to create hardware device context: %s", errbuf);
			us_ffmpeg_hwenc_destroy(enc);
			return US_HWENC_ERROR_DEVICE_NOT_FOUND;
		}

		enc->ctx->hw_device_ctx = av_buffer_ref(enc->hw_device_ctx);

		// 对于VAAPI，设置像素格式
		if (type == US_HWENC_VAAPI) {
			enc->ctx->pix_fmt = AV_PIX_FMT_VAAPI;
		}
	}

	// 设置编码器选项
	AVDictionary *opts = NULL;
	if (type == US_HWENC_LIBX264) {
		// libx264软件编码器选项 - 基础设置
		av_dict_set(&opts, "preset", "ultrafast", 0);
		av_dict_set(&opts, "tune", "zerolatency", 0);
		av_dict_set(&opts, "profile", "baseline", 0);
		av_dict_set(&opts, "crf", "23", 0);  // 默认质量
	} else if (type == US_HWENC_VAAPI) {
		// VAAPI硬件编码器选项 - 使用CBR模式确保码率生效
		av_dict_set(&opts, "rc_mode", "CBR", 0);               // 恒定码率模式
		av_dict_set(&opts, "packed_headers", "none", 0);       // 禁用打包头
		// 设置关键帧间隔，确保关键帧生成
		char gop_str[16];
		snprintf(gop_str, sizeof(gop_str), "%u", gop_size);
		av_dict_set(&opts, "g", gop_str, 0);                  // 设置GOP大小
		av_dict_set(&opts, "keyint_min", gop_str, 0);         // 设置最小关键帧间隔
		// 不设置profile和level，让驱动自动选择最兼容的配置
	} else if (type == US_HWENC_RKMPP) {
		// RKMPP (Rockchip MPP) 硬件编码器选项
		av_dict_set(&opts, "rc_mode", "1", 0);                // CBR模式 (0=VBR, 1=CBR, 2=CQP, 3=AVBR)
		av_dict_set(&opts, "profile", "100", 0);              // High profile
		av_dict_set(&opts, "level", "40", 0);                 // Level 4.0
		av_dict_set(&opts, "coder", "1", 0);                  // CABAC熵编码器
		// 设置关键帧间隔
		char gop_str[16];
		snprintf(gop_str, sizeof(gop_str), "%u", gop_size);
		av_dict_set(&opts, "g", gop_str, 0);                  // GOP大小
		// QP参数优化
		av_dict_set(&opts, "qp_init", "24", 0);               // 初始QP值
		av_dict_set(&opts, "qp_min", "16", 0);                // 最小QP值
		av_dict_set(&opts, "qp_max", "40", 0);                // 最大QP值
	} else if (type == US_HWENC_NVENC) {
		av_dict_set(&opts, "preset", "fast", 0);
		av_dict_set(&opts, "profile", "main", 0);
	}

	// 对于VAAPI，在打开编码器前设置硬件帧上下文
	if (type == US_HWENC_VAAPI) {
		// 创建硬件帧上下文（按FFmpeg官方示例）
		AVBufferRef *hw_frames_ref = av_hwframe_ctx_alloc(enc->hw_device_ctx);
		if (!hw_frames_ref) {
			US_LOG_ERROR("HWENC: Failed to create VAAPI frame context");
			av_dict_free(&opts);
			us_ffmpeg_hwenc_destroy(enc);
			return US_HWENC_ERROR_MEMORY;
		}
		
		AVHWFramesContext *frames_ctx = (AVHWFramesContext *)(hw_frames_ref->data);
		frames_ctx->format = AV_PIX_FMT_VAAPI;
		frames_ctx->sw_format = AV_PIX_FMT_NV12;
		frames_ctx->width = width;
		frames_ctx->height = height;
		frames_ctx->initial_pool_size = 20;
		
		int vaapi_ret = av_hwframe_ctx_init(hw_frames_ref);
		if (vaapi_ret < 0) {
			char errbuf[AV_ERROR_MAX_STRING_SIZE];
			av_strerror(vaapi_ret, errbuf, sizeof(errbuf));
			US_LOG_ERROR("HWENC: Failed to initialize VAAPI frame context: %s", errbuf);
			av_buffer_unref(&hw_frames_ref);
			av_dict_free(&opts);
			us_ffmpeg_hwenc_destroy(enc);
			return US_HWENC_ERROR_DEVICE_NOT_FOUND;
		}
		
		enc->ctx->hw_frames_ctx = av_buffer_ref(hw_frames_ref);
		av_buffer_unref(&hw_frames_ref);
		
		if (!enc->ctx->hw_frames_ctx) {
			US_LOG_ERROR("HWENC: Failed to reference hardware frames context");
			av_dict_free(&opts);
			us_ffmpeg_hwenc_destroy(enc);
			return US_HWENC_ERROR_MEMORY;
		}
	}
	
	// 打开编码器
	int ret = avcodec_open2(enc->ctx, codec, &opts);
	av_dict_free(&opts);
	if (ret < 0) {
		char errbuf[AV_ERROR_MAX_STRING_SIZE];
		av_strerror(ret, errbuf, sizeof(errbuf));
		US_LOG_ERROR("HWENC: Failed to open codec: %s", errbuf);
		
		// 对于VAAPI，如果初始化失败，记录详细错误信息
		if (type == US_HWENC_VAAPI) {
			US_LOG_ERROR("HWENC: VAAPI initialization failed, this may be due to:");
			US_LOG_ERROR("HWENC: - Incompatible driver (try updating mesa/intel-media-driver)");
			US_LOG_ERROR("HWENC: - Missing VAAPI permissions (check /dev/dri access)");
			US_LOG_ERROR("HWENC: - Unsupported hardware profile");
		}
		
		us_ffmpeg_hwenc_destroy(enc);
		return US_HWENC_ERROR_ENCODER_INIT;
	}

	// 分配帧和包
	enc->frame = av_frame_alloc();
	enc->pkt = av_packet_alloc();
	if (!enc->frame || !enc->pkt) {
		US_LOG_ERROR("HWENC: Failed to allocate frame or packet");
		us_ffmpeg_hwenc_destroy(enc);
		return US_HWENC_ERROR_MEMORY;
	}

	// 设置帧参数
	enc->frame->format = enc->ctx->pix_fmt;
	enc->frame->width = width;
	enc->frame->height = height;

	// 对于VAAPI硬件编码，不在初始化时分配帧缓冲（按需分配）
	// 对于软件编码，预分配缓冲区以提高性能
	if (type != US_HWENC_VAAPI) {
		ret = av_frame_get_buffer(enc->frame, 32);
		if (ret < 0) {
			char errbuf[AV_ERROR_MAX_STRING_SIZE];
			av_strerror(ret, errbuf, sizeof(errbuf));
			US_LOG_ERROR("HWENC: Failed to allocate frame buffer: %s", errbuf);
			us_ffmpeg_hwenc_destroy(enc);
			return US_HWENC_ERROR_MEMORY;
		}
	}

	// 软件缩放器将在编码时根据输入格式动态创建
	enc->sws_ctx = NULL;

	enc->initialized = true;
	*encoder = enc;

	US_LOG_INFO("HWENC: Hardware encoder created successfully (%s, %dx%d @ %u kbps)",
	           enc->codec_name, width, height, bitrate_kbps);

	return US_HWENC_OK;
}

// 扩展版本，支持libx264预设参数
us_hwenc_error_e us_ffmpeg_hwenc_create_with_preset(us_ffmpeg_hwenc_s **encoder,
                                                   us_hwenc_type_e type,
                                                   int width, int height,
                                                   uint bitrate_kbps, uint gop_size,
                                                   const char *preset, const char *tune, const char *profile) {
	if (!encoder) {
		US_LOG_ERROR("HWENC: Encoder pointer is NULL");
		return US_HWENC_ERROR_INVALID_PARAM;
	}
	
	*encoder = NULL;

	// 分配编码器结构体
	us_ffmpeg_hwenc_s *enc = calloc(1, sizeof(us_ffmpeg_hwenc_s));
	if (!enc) {
		US_LOG_ERROR("HWENC: Failed to allocate encoder structure");
		return US_HWENC_ERROR_MEMORY;
	}

	// 初始化互斥锁
	if (pthread_mutex_init(&enc->mutex, NULL) != 0) {
		US_LOG_ERROR("HWENC: Failed to initialize mutex");
		free(enc);
		return US_HWENC_ERROR_MEMORY;
	}
	enc->mutex_initialized = true;

	// 设置基础参数
	enc->type = type;
	enc->width = width;
	enc->height = height;
	enc->bitrate_kbps = bitrate_kbps;
	enc->gop_size = gop_size;
	strncpy(enc->codec_name, us_ffmpeg_hwenc_get_codec_name(type), sizeof(enc->codec_name) - 1);

	// 保存libx264预设参数
	if (preset) {
		strncpy(enc->preset, preset, sizeof(enc->preset) - 1);
		enc->preset[sizeof(enc->preset) - 1] = '\0';
	}
	if (tune) {
		strncpy(enc->tune, tune, sizeof(enc->tune) - 1);
		enc->tune[sizeof(enc->tune) - 1] = '\0';
	}
	if (profile) {
		strncpy(enc->profile, profile, sizeof(enc->profile) - 1);
		enc->profile[sizeof(enc->profile) - 1] = '\0';
	}

	// 查找编码器
	const AVCodec *codec = avcodec_find_encoder_by_name(enc->codec_name);
	if (!codec) {
		US_LOG_ERROR("HWENC: Codec %s not found", enc->codec_name);
		us_ffmpeg_hwenc_destroy(enc);
		return US_HWENC_ERROR_ENCODER_INIT;
	}

	// 创建编码器上下文
	enc->ctx = avcodec_alloc_context3(codec);
	if (!enc->ctx) {
		US_LOG_ERROR("HWENC: Failed to allocate codec context");
		us_ffmpeg_hwenc_destroy(enc);
		return US_HWENC_ERROR_MEMORY;
	}

	// 配置编码器参数
	enc->ctx->width = width;
	enc->ctx->height = height;
	enc->ctx->time_base = (AVRational){1, 25};
	enc->ctx->framerate = (AVRational){25, 1};
	enc->ctx->bit_rate = bitrate_kbps * 1000;
	enc->ctx->gop_size = gop_size;
	enc->ctx->max_b_frames = 0;
	enc->ctx->pix_fmt = AV_PIX_FMT_YUV420P;

	// 设置编码器选项
	AVDictionary *opts = NULL;
	if (type == US_HWENC_LIBX264) {
		// 根据用户preset优化libx264参数
		const char *use_preset = preset ? preset : "ultrafast";
		int thread_count = _get_optimal_threads_by_preset(use_preset, width, height);
		
		// 设置线程数
		char threads_str[8];
		snprintf(threads_str, sizeof(threads_str), "%d", thread_count);
		av_dict_set(&opts, "threads", threads_str, 0);
		
		// 设置preset
		av_dict_set(&opts, "preset", use_preset, 0);
		
		// 设置tune
		const char *use_tune = tune ? tune : "zerolatency";
		av_dict_set(&opts, "tune", use_tune, 0);
		
		// 设置profile
		const char *use_profile = profile ? profile : "baseline";
		av_dict_set(&opts, "profile", use_profile, 0);
		
		// 根据preset优化编码复杂度参数（但保持充分利用多核心）
		if (strcmp(use_preset, "ultrafast") == 0) {
			// 低性能设备：降低编码复杂度而非线程数
			av_dict_set(&opts, "crf", "26", 0);     // 适当限制质量减少计算量
			av_dict_set(&opts, "me_method", "dia", 0);  // 最快运动估计算法
			av_dict_set(&opts, "subme", "1", 0);    // 最低子像素运动估计
			av_dict_set(&opts, "refs", "1", 0);     // 单参考帧减少内存使用
			av_dict_set(&opts, "trellis", "0", 0);  // 禁用trellis量化节省CPU
			av_dict_set(&opts, "rc_lookahead", "0", 0); // 禁用前瞻减少延迟
			av_dict_set(&opts, "mixed_refs", "0", 0);   // 禁用混合参考
			av_dict_set(&opts, "8x8dct", "0", 0);       // 禁用8x8 DCT变换
			av_dict_set(&opts, "cabac", "0", 0);        // 禁用CABAC熔码
			av_dict_set(&opts, "deblock", "0:0", 0);    // 禁用去块效滤器
		} else if (strcmp(use_preset, "veryfast") == 0 || strcmp(use_preset, "faster") == 0) {
			// 中等性能设备：适度的编码复杂度
			av_dict_set(&opts, "crf", "24", 0);
			av_dict_set(&opts, "me_method", "hex", 0);  // 六边形运动估计
			av_dict_set(&opts, "subme", "2", 0);    // 低级子像素运动估计
			av_dict_set(&opts, "refs", "2", 0);     // 双参考帧
			av_dict_set(&opts, "rc_lookahead", "10", 0); // 少量前瞻
			av_dict_set(&opts, "trellis", "0", 0);  // 仍禁用trellis保持速度
		} else if (strcmp(use_preset, "fast") == 0) {
			// 高性能设备：更好的质量设置
			av_dict_set(&opts, "crf", "22", 0);
			av_dict_set(&opts, "me_method", "umh", 0);  // 非对称多六边形运动估计
			av_dict_set(&opts, "subme", "4", 0);    // 中级子像素运动估计
			av_dict_set(&opts, "refs", "3", 0);     // 三参考帧
			av_dict_set(&opts, "rc_lookahead", "20", 0); // 适度前瞻
			av_dict_set(&opts, "trellis", "1", 0);  // 启用基础trellis
		} else {
			// medium及更慢预设：最优质量设置
			av_dict_set(&opts, "crf", "20", 0);     // 更低的CRF获得更好质量
			av_dict_set(&opts, "me_method", "umh", 0);
			av_dict_set(&opts, "subme", "6", 0);    // 高级子像素运动估计
			av_dict_set(&opts, "refs", "4", 0);     // 四参考帧
			av_dict_set(&opts, "rc_lookahead", "40", 0); // 更多前瞻优化
			av_dict_set(&opts, "trellis", "2", 0);  // 完整trellis量化
		}
		
		// 实时编码通用优化（适用于所有preset）
		av_dict_set(&opts, "slice_max_size", "1500", 0);  // 限制slice大小减少延迟
		av_dict_set(&opts, "intra_refresh", "1", 0);     // 启用帧内刷新
		av_dict_set(&opts, "sliced_threads", "1", 0);    // 启用slice级多线程
		av_dict_set(&opts, "thread_type", "slice", 0);   // 优化线程类型
		
		US_LOG_INFO("HWENC: Optimized libx264 with preset=%s (%d threads, %d CPU cores), tune=%s, profile=%s", 
		           use_preset, thread_count, _get_cpu_core_count(), use_tune, use_profile);
	} else if (type == US_HWENC_VAAPI) {
		// VAAPI硬件编码器选项 - 使用CBR模式确保码率生效
		av_dict_set(&opts, "rc_mode", "CBR", 0);
		av_dict_set(&opts, "packed_headers", "none", 0);
		// 设置关键帧间隔
		char gop_str[16];
		snprintf(gop_str, sizeof(gop_str), "%u", gop_size);
		av_dict_set(&opts, "g", gop_str, 0);
		av_dict_set(&opts, "keyint_min", gop_str, 0);
	} else if (type == US_HWENC_RKMPP) {
		// RKMPP (Rockchip MPP) 硬件编码器选项 - 与preset版本相同配置
		av_dict_set(&opts, "rc_mode", "1", 0);                // CBR模式 (0=VBR, 1=CBR, 2=CQP, 3=AVBR)
		av_dict_set(&opts, "profile", "100", 0);              // High profile
		av_dict_set(&opts, "level", "40", 0);                 // Level 4.0
		av_dict_set(&opts, "coder", "1", 0);                  // CABAC熵编码器
		// 设置关键帧间隔
		char gop_str[16];
		snprintf(gop_str, sizeof(gop_str), "%u", gop_size);
		av_dict_set(&opts, "g", gop_str, 0);                  // GOP大小
		// QP参数优化
		av_dict_set(&opts, "qp_init", "24", 0);               // 初始QP值
		av_dict_set(&opts, "qp_min", "16", 0);                // 最小QP值
		av_dict_set(&opts, "qp_max", "40", 0);                // 最大QP值
	} else if (type == US_HWENC_NVENC) {
		av_dict_set(&opts, "preset", "fast", 0);
		av_dict_set(&opts, "profile", "main", 0);
	}

	// 硬件设备初始化（与原有代码相同）
	const char *hw_device_type = _get_hw_device_type(type);
	if (hw_device_type) {
		enum AVHWDeviceType device_type = av_hwdevice_find_type_by_name(hw_device_type);
		if (device_type == AV_HWDEVICE_TYPE_NONE) {
			US_LOG_ERROR("HWENC: Hardware device type %s not found", hw_device_type);
			av_dict_free(&opts);
			us_ffmpeg_hwenc_destroy(enc);
			return US_HWENC_ERROR_DEVICE_NOT_FOUND;
		}

		const char *device_name = NULL;
		if (type == US_HWENC_VAAPI) {
			device_name = "/dev/dri/renderD128";
		}
		
		int ret = av_hwdevice_ctx_create(&enc->hw_device_ctx, device_type, device_name, NULL, 0);
		if (ret < 0) {
			char errbuf[AV_ERROR_MAX_STRING_SIZE];
			av_strerror(ret, errbuf, sizeof(errbuf));
			US_LOG_ERROR("HWENC: Failed to create hardware device context: %s", errbuf);
			av_dict_free(&opts);
			us_ffmpeg_hwenc_destroy(enc);
			return US_HWENC_ERROR_DEVICE_NOT_FOUND;
		}

		enc->ctx->hw_device_ctx = av_buffer_ref(enc->hw_device_ctx);

		if (type == US_HWENC_VAAPI) {
			enc->ctx->pix_fmt = AV_PIX_FMT_VAAPI;
			
			// VAAPI硬件帧上下文设置
			AVBufferRef *hw_frames_ref = av_hwframe_ctx_alloc(enc->hw_device_ctx);
			if (!hw_frames_ref) {
				US_LOG_ERROR("HWENC: Failed to create VAAPI frame context");
				av_dict_free(&opts);
				us_ffmpeg_hwenc_destroy(enc);
				return US_HWENC_ERROR_MEMORY;
			}
			
			AVHWFramesContext *frames_ctx = (AVHWFramesContext *)(hw_frames_ref->data);
			frames_ctx->format = AV_PIX_FMT_VAAPI;
			frames_ctx->sw_format = AV_PIX_FMT_NV12;
			frames_ctx->width = width;
			frames_ctx->height = height;
			frames_ctx->initial_pool_size = 20;
			
			ret = av_hwframe_ctx_init(hw_frames_ref);
			if (ret < 0) {
				char errbuf[AV_ERROR_MAX_STRING_SIZE];
				av_strerror(ret, errbuf, sizeof(errbuf));
				US_LOG_ERROR("HWENC: Failed to initialize VAAPI frame context: %s", errbuf);
				av_buffer_unref(&hw_frames_ref);
				av_dict_free(&opts);
				us_ffmpeg_hwenc_destroy(enc);
				return US_HWENC_ERROR_DEVICE_NOT_FOUND;
			}
			
			enc->ctx->hw_frames_ctx = av_buffer_ref(hw_frames_ref);
			av_buffer_unref(&hw_frames_ref);
			
			if (!enc->ctx->hw_frames_ctx) {
				US_LOG_ERROR("HWENC: Failed to reference hardware frames context");
				av_dict_free(&opts);
				us_ffmpeg_hwenc_destroy(enc);
				return US_HWENC_ERROR_MEMORY;
			}
		}
	}

	// 打开编码器
	int ret = avcodec_open2(enc->ctx, codec, &opts);
	av_dict_free(&opts);
	if (ret < 0) {
		char errbuf[AV_ERROR_MAX_STRING_SIZE];
		av_strerror(ret, errbuf, sizeof(errbuf));
		US_LOG_ERROR("HWENC: Failed to open codec: %s", errbuf);
		
		if (type == US_HWENC_VAAPI) {
			US_LOG_ERROR("HWENC: VAAPI initialization failed, this may be due to:");
			US_LOG_ERROR("HWENC: - Incompatible driver (try updating mesa/intel-media-driver)");
			US_LOG_ERROR("HWENC: - Missing VAAPI permissions (check /dev/dri access)");
			US_LOG_ERROR("HWENC: - Unsupported hardware profile");
		}
		
		us_ffmpeg_hwenc_destroy(enc);
		return US_HWENC_ERROR_ENCODER_INIT;
	}

	// 分配帧和包
	enc->frame = av_frame_alloc();
	enc->pkt = av_packet_alloc();
	if (!enc->frame || !enc->pkt) {
		US_LOG_ERROR("HWENC: Failed to allocate frame or packet");
		us_ffmpeg_hwenc_destroy(enc);
		return US_HWENC_ERROR_MEMORY;
	}

	// 设置帧参数
	enc->frame->format = enc->ctx->pix_fmt;
	enc->frame->width = width;
	enc->frame->height = height;

	// 对于VAAPI硬件编码，不在初始化时分配帧缓冲
	if (type != US_HWENC_VAAPI) {
		ret = av_frame_get_buffer(enc->frame, 32);
		if (ret < 0) {
			char errbuf[AV_ERROR_MAX_STRING_SIZE];
			av_strerror(ret, errbuf, sizeof(errbuf));
			US_LOG_ERROR("HWENC: Failed to allocate frame buffer: %s", errbuf);
			us_ffmpeg_hwenc_destroy(enc);
			return US_HWENC_ERROR_MEMORY;
		}
	}

	// 软件缩放器将在编码时根据输入格式动态创建
	enc->sws_ctx = NULL;

	enc->initialized = true;
	*encoder = enc;

	US_LOG_INFO("HWENC: Hardware encoder created successfully (%s, %dx%d @ %u kbps)",
	           enc->codec_name, width, height, bitrate_kbps);

	return US_HWENC_OK;
}

// 编码一帧
us_hwenc_error_e us_ffmpeg_hwenc_compress(us_ffmpeg_hwenc_s *encoder,
                                         const us_frame_s *src,
                                         us_frame_s *dest,
                                         bool force_key) {
	US_HWENC_CHECK_NOT_NULL(encoder, "Encoder");
	US_HWENC_CHECK_NOT_NULL(src, "Source frame");
	US_HWENC_CHECK_NOT_NULL(dest, "Destination frame");
	US_HWENC_CHECK_PARAM(encoder->initialized, US_HWENC_ERROR_NOT_INITIALIZED, "Encoder not initialized");

	pthread_mutex_lock(&encoder->mutex);

	uint64_t start_time = us_get_now_monotonic_u64();

	// 检查输入帧格式
	if (src->format != V4L2_PIX_FMT_RGB24 && src->format != V4L2_PIX_FMT_YUYV) {
		pthread_mutex_unlock(&encoder->mutex);
		US_LOG_ERROR("HWENC: Unsupported input format: %u", src->format);
		return US_HWENC_ERROR_FORMAT_UNSUPPORTED;
	}

	// 确定输入和输出格式
	enum AVPixelFormat input_format;
	enum AVPixelFormat output_format = (encoder->type == US_HWENC_VAAPI) ? AV_PIX_FMT_NV12 : AV_PIX_FMT_YUV420P;
	
	// 对于VAAPI，确保使用NV12格式以匹配硬件帧上下文
	if (encoder->type == US_HWENC_VAAPI) {
		output_format = AV_PIX_FMT_NV12;
	}
	
	if (src->format == V4L2_PIX_FMT_RGB24) {
		input_format = AV_PIX_FMT_RGB24;
	} else if (src->format == V4L2_PIX_FMT_YUYV) {
		input_format = AV_PIX_FMT_YUYV422;
	} else {
		pthread_mutex_unlock(&encoder->mutex);
		US_LOG_ERROR("HWENC: Unsupported format conversion from: %u", src->format);
		return US_HWENC_ERROR_FORMAT_UNSUPPORTED;
	}

	// 动态创建或更新软件缩放器
	if (!encoder->sws_ctx) {
		encoder->sws_ctx = sws_getContext(
			encoder->width, encoder->height, input_format,
			encoder->width, encoder->height, output_format,
			SWS_BILINEAR, NULL, NULL, NULL);
		
		if (!encoder->sws_ctx) {
			pthread_mutex_unlock(&encoder->mutex);
			US_LOG_ERROR("HWENC: Failed to create software scaler");
			return US_HWENC_ERROR_MEMORY;
		}
		
		US_LOG_DEBUG("HWENC: Created scaler %s -> %s", 
		           av_get_pix_fmt_name(input_format),
		           av_get_pix_fmt_name(output_format));
	}

	// 准备输入数据
	const uint8_t *src_data[4] = {src->data, NULL, NULL, NULL};
	int src_linesize[4] = {(int)src->stride, 0, 0, 0};

	// 分配软件帧缓冲区
	AVFrame *yuv_frame = av_frame_alloc();
	if (!yuv_frame) {
		pthread_mutex_unlock(&encoder->mutex);
		return US_HWENC_ERROR_MEMORY;
	}

	yuv_frame->format = output_format;
	yuv_frame->width = encoder->width;
	yuv_frame->height = encoder->height;

	int ret = av_frame_get_buffer(yuv_frame, 32);
	if (ret < 0) {
		av_frame_free(&yuv_frame);
		pthread_mutex_unlock(&encoder->mutex);
		return US_HWENC_ERROR_MEMORY;
	}

	// 格式转换
	sws_scale(encoder->sws_ctx,
		src_data, src_linesize,
		0, encoder->height,
		yuv_frame->data, yuv_frame->linesize);

	// 设置帧时间戳
	yuv_frame->pts = encoder->frame_number;
	
	// 强制关键帧（第一帧或明确请求）
	if (force_key || encoder->frame_number == 0) {
		yuv_frame->pict_type = AV_PICTURE_TYPE_I;
	}
	
	encoder->frame_number++;

	AVFrame *hw_frame = yuv_frame;
	
	// 对于VAAPI，需要上传到硬件帧
	if (encoder->type == US_HWENC_VAAPI) {
		hw_frame = av_frame_alloc();
		if (!hw_frame) {
			av_frame_free(&yuv_frame);
			pthread_mutex_unlock(&encoder->mutex);
			return US_HWENC_ERROR_MEMORY;
		}
		
		ret = av_hwframe_get_buffer(encoder->ctx->hw_frames_ctx, hw_frame, 0);
		if (ret < 0) {
			char errbuf[AV_ERROR_MAX_STRING_SIZE];
			av_strerror(ret, errbuf, sizeof(errbuf));
			US_LOG_ERROR("HWENC: Failed to allocate VAAPI frame: %s", errbuf);
			av_frame_free(&hw_frame);
			av_frame_free(&yuv_frame);
			pthread_mutex_unlock(&encoder->mutex);
			return US_HWENC_ERROR_MEMORY;
		}
		
		// 验证硬件帧上下文（按FFmpeg官方示例）
		if (!hw_frame->hw_frames_ctx) {
			US_LOG_ERROR("HWENC: Hardware frame context not set");
			av_frame_free(&hw_frame);
			av_frame_free(&yuv_frame);
			pthread_mutex_unlock(&encoder->mutex);
			return US_HWENC_ERROR_MEMORY;
		}
		
		ret = av_hwframe_transfer_data(hw_frame, yuv_frame, 0);
		if (ret < 0) {
			char errbuf[AV_ERROR_MAX_STRING_SIZE];
			av_strerror(ret, errbuf, sizeof(errbuf));
			US_LOG_ERROR("HWENC: Failed to transfer data to VAAPI frame: %s", errbuf);
			av_frame_free(&hw_frame);
			av_frame_free(&yuv_frame);
			pthread_mutex_unlock(&encoder->mutex);
			return US_HWENC_ERROR_ENCODE;
		}
		
		hw_frame->pts = yuv_frame->pts;
		if (force_key || yuv_frame->pict_type == AV_PICTURE_TYPE_I) {
			hw_frame->pict_type = AV_PICTURE_TYPE_I;
		}
	}

	// 发送帧到编码器
	ret = avcodec_send_frame(encoder->ctx, hw_frame);
	
	// 清理帧
	if (encoder->type == US_HWENC_VAAPI && hw_frame != yuv_frame) {
		av_frame_free(&hw_frame);
	}
	av_frame_free(&yuv_frame);

	if (ret < 0) {
		pthread_mutex_unlock(&encoder->mutex);
		encoder->stats.encode_errors++;
		char errbuf[AV_ERROR_MAX_STRING_SIZE];
		av_strerror(ret, errbuf, sizeof(errbuf));
		US_LOG_ERROR("HWENC: Failed to send frame: %s", errbuf);
		return US_HWENC_ERROR_ENCODE;
	}

	// 接收编码包
	ret = avcodec_receive_packet(encoder->ctx, encoder->pkt);
	if (ret < 0) {
		pthread_mutex_unlock(&encoder->mutex);
		if (ret == AVERROR(EAGAIN)) {
			// 需要更多帧数据，这是正常情况
			return US_HWENC_OK;
		}
		encoder->stats.encode_errors++;
		char errbuf[AV_ERROR_MAX_STRING_SIZE];
		av_strerror(ret, errbuf, sizeof(errbuf));
		US_LOG_ERROR("HWENC: Failed to receive packet: %s", errbuf);
		return US_HWENC_ERROR_ENCODE;
	}

	// 复制编码数据到目标帧
	if (encoder->pkt->size > 0) {
		if (dest->allocated < (size_t)encoder->pkt->size) {
			dest->data = realloc(dest->data, encoder->pkt->size);
			if (!dest->data) {
				av_packet_unref(encoder->pkt);
				pthread_mutex_unlock(&encoder->mutex);
				return US_HWENC_ERROR_MEMORY;
			}
			dest->allocated = encoder->pkt->size;
		}
		
		memcpy(dest->data, encoder->pkt->data, encoder->pkt->size);
		dest->used = encoder->pkt->size;
		dest->format = V4L2_PIX_FMT_H264;
		dest->width = encoder->width;
		dest->height = encoder->height;
		
		// 更新统计信息
		encoder->stats.frames_encoded++;
		encoder->stats.bytes_output += encoder->pkt->size;
		
		uint64_t end_time = us_get_now_monotonic_u64();
		double encode_time_ms = (end_time - start_time) / 1000.0;
		encoder->stats.total_encode_time_ms += encode_time_ms;
		encoder->stats.avg_encode_time_ms = encoder->stats.total_encode_time_ms / encoder->stats.frames_encoded;
		
		// 检查是否是关键帧并设置标记
		bool is_keyframe = (encoder->pkt->flags & AV_PKT_FLAG_KEY) != 0;
		dest->key = is_keyframe ? 1 : 0;
		
		US_LOG_DEBUG("HWENC: Encoded frame %lu, size: %d bytes, time: %.2fms", 
		           encoder->stats.frames_encoded, encoder->pkt->size, encode_time_ms);
	}

	av_packet_unref(encoder->pkt);
	pthread_mutex_unlock(&encoder->mutex);
	
	return US_HWENC_OK;
}

// 获取统计信息
us_hwenc_error_e us_ffmpeg_hwenc_get_stats(us_ffmpeg_hwenc_s *encoder, us_hwenc_stats_s *stats) {
	if (!encoder || !stats) {
		return US_HWENC_ERROR_INVALID_PARAM;
	}
	
	pthread_mutex_lock(&encoder->mutex);
	memcpy(stats, &encoder->stats, sizeof(*stats));
	pthread_mutex_unlock(&encoder->mutex);
	
	return US_HWENC_OK;
}

// 销毁编码器
void us_ffmpeg_hwenc_destroy(us_ffmpeg_hwenc_s *encoder) {
	if (!encoder) return;

	US_LOG_DEBUG("HWENC: Destroying encoder (%s)", us_hwenc_type_to_string(encoder->type));

	// 清理FFmpeg资源
	if (encoder->sws_ctx) {
		sws_freeContext(encoder->sws_ctx);
		encoder->sws_ctx = NULL;
	}
	
	if (encoder->ctx) {
		avcodec_close(encoder->ctx);
		avcodec_free_context(&encoder->ctx);
	}
	
	if (encoder->frame) {
		av_frame_free(&encoder->frame);
	}
	
	if (encoder->pkt) {
		av_packet_free(&encoder->pkt);
	}
	
	if (encoder->hw_device_ctx) {
		av_buffer_unref(&encoder->hw_device_ctx);
	}

	// 清理互斥锁
	if (encoder->mutex_initialized) {
		pthread_mutex_destroy(&encoder->mutex);
	}

	// 输出最终统计信息
	if (encoder->stats.frames_encoded > 0) {
		US_LOG_INFO("HWENC: Final stats - Frames: %lu, Output: %lu bytes, Errors: %lu, Avg time: %.2fms",
		           encoder->stats.frames_encoded, encoder->stats.bytes_output, 
		           encoder->stats.encode_errors, encoder->stats.avg_encode_time_ms);
	}

	free(encoder);
}

#else // !WITH_FFMPEG

// FFmpeg未编译时的空实现
const char* us_hwenc_type_to_string(us_hwenc_type_e type) {
	return "no-ffmpeg";
}

us_hwenc_error_e us_ffmpeg_hwenc_create(us_ffmpeg_hwenc_s **encoder,
                                       us_hwenc_type_e type,
                                       int width, int height,
                                       uint bitrate_kbps, uint gop_size) {
	US_LOG_ERROR("HWENC: FFmpeg support not compiled");
	return US_HWENC_ERROR_FFMPEG_ERROR;
}

us_hwenc_error_e us_ffmpeg_hwenc_create_with_preset(us_ffmpeg_hwenc_s **encoder,
                                                   us_hwenc_type_e type,
                                                   int width, int height,
                                                   uint bitrate_kbps, uint gop_size,
                                                   const char *preset, const char *tune, const char *profile) {
	US_LOG_ERROR("HWENC: FFmpeg support not compiled");
	return US_HWENC_ERROR_FFMPEG_ERROR;
}

us_hwenc_error_e us_ffmpeg_hwenc_compress(us_ffmpeg_hwenc_s *encoder,
                                         const us_frame_s *src,
                                         us_frame_s *dest,
                                         bool force_key) {
	US_LOG_ERROR("HWENC: FFmpeg support not compiled");
	return US_HWENC_ERROR_FFMPEG_ERROR;
}

us_hwenc_error_e us_ffmpeg_hwenc_get_stats(us_ffmpeg_hwenc_s *encoder, us_hwenc_stats_s *stats) {
	US_LOG_ERROR("HWENC: FFmpeg support not compiled");
	return US_HWENC_ERROR_FFMPEG_ERROR;
}

void us_ffmpeg_hwenc_destroy(us_ffmpeg_hwenc_s *encoder) {
	// 空实现
}

#endif // WITH_FFMPEG

// 格式支持检查
bool us_ffmpeg_hwenc_is_format_supported(us_hwenc_type_e encoder_type, uint32_t format) {
	(void)encoder_type; // 避免编译器警告
	switch (format) {
		case V4L2_PIX_FMT_RGB24:
		case V4L2_PIX_FMT_YUYV:
			return true;
		default:
			return false;
	}
}