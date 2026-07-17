/*****************************************************************************
#                                                                            #
#    uStreamer - Lightweight and fast MJPEG-HTTP streamer.                   #
#                                                                            #
#    This source file based on code of Janys-Gateway.                        #
#                                                                            #
#    Copyright (C)  Lorenzo Miniero <lorenzo@meetecho.com>                   #
#    Copyright (C) 2018-2024  Maxim Devaev <mdevaev@gmail.com>               #
#                                                                            #
#    This program is free software: you can redistribute it and/or modify    #
#    it under the terms of the GNU General Public License as published by    #
#    the Free Software Foundation, either version 3 of the License, or       #
#    (at your option) any later version.                                     #
#                                                                            #
#    This program is distributed in the hope that it will be useful,         #
#    but WITHOUT ANY WARRANTY; without even the implied warranty of          #
#    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the           #
#    GNU General Public License for more details.                            #
#                                                                            #
#    You should have received a copy of the GNU General Public License       #
#    along with this program.  If not, see <https://www.gnu.org/licenses/>.  #
#                                                                            #
*****************************************************************************/


#include "h264.h"

#include "uslibs/types.h"
#include "uslibs/tools.h"


static INLINE u32 _eg_get_bit(const u8 *buf, u32 bit_offset) {
	return ((*(buf + (bit_offset >> 3))) >> (7 - (bit_offset & 0x07))) & 0x01;
}

static INLINE bool _is_valid_eg_offset(u32 bit_offset, uz size) {
	return (bit_offset >> 3) + (bit_offset % 8 ? 1 : 0) < size;
}

static bool _eg_skip_u1(const u8 *buf, u32 *bit_offset, uz size) {
	(void)buf;
	if (!_is_valid_eg_offset(*bit_offset, size)) {
		return false;
	}
	++(*bit_offset);
	return true;
}

static bool _eg_decode_u1(const u8 *buf, u32 *bit_offset, uz size, bool *out) {
	if (!_is_valid_eg_offset(*bit_offset, size)) {
		return false;
	}
	*out = (_eg_get_bit(buf, *bit_offset) != 0);
	++(*bit_offset);
	return true;
}

static bool _eg_skip_ev(const u8 *buf, u32 *bit_offset, uz size) {
	u32 zeros = 0;
	while (true) {
		if (!_is_valid_eg_offset(*bit_offset, size)) {
			return false;
		}
		if (_eg_get_bit(buf, (*bit_offset)++) != 0) {
			break;
		}
		++zeros;
	}
	for (; zeros; --zeros) {
		if (!_is_valid_eg_offset((*bit_offset)++, size)) {
			return false;
		}
	}
	return true;
}

static bool _eg_decode_uev(const u8 *buf, u32 *bit_offset, uint size, u32 *out) {
	u32 zeros = 0;
	while (true) {
		if (!_is_valid_eg_offset(*bit_offset, size)) {
			return false;
		}
		if (_eg_get_bit(buf, (*bit_offset)++) != 0) {
			break;
		}
		++zeros;
	}
	u32 res = (1 << zeros);
	for (; zeros; --zeros) {
		if (!_is_valid_eg_offset(*bit_offset, size)) {
			return false;
		}
		res |= (_eg_get_bit(buf, *bit_offset) << (zeros - 1));
		++(*bit_offset);
	}
	*out = res - 1;
	return true;
}

int us_h264_parse_sps_nalu(const u8 *buf, uz size, uint *width, uint *height) {
	// NAL header + profile_idc + constraint_setX_flag, reserved_zero_2bits + level_idc
	if (size < 4) {
		return -1;
	}

	// NAL header
	++buf;
	--size;

	// profile_idc u(8) - Baseline profile
	const u8 profile_idc = *buf;
	if (profile_idc != 66) {
		return -1;
	}
	++buf;
	--size;

	// constraint_set0_flag u(1)
	// constraint_set1_flag u(1)
	// constraint_set2_flag u(1)
	// constraint_set3_flag u(1)
	// constraint_set4_flag u(1)
	// constraint_set5_flag u(1)
	// reserved_zero_2bits u(2)
	++buf;
	--size;

	// level_idc u(8)
	++buf;
	--size;

	u32 bit_offset = 0;

	// seq_parameter_set_id ue(v)
	if (!_eg_skip_ev(buf, &bit_offset, size)) {
		return -1;
	}

	// log2_max_frame_num_minus4 ue(v)
	u32 log2_max_frame_num_minus4;
	if (!_eg_decode_uev(buf, &bit_offset, size, &log2_max_frame_num_minus4)) {
		return -1;
	}

	// pic_order_cnt_type ue(v)
	u32 pic_order_cnt_type;
	if (!_eg_decode_uev(buf, &bit_offset, size, &pic_order_cnt_type)) {
		return -1;
	}

	if (pic_order_cnt_type == 0) {
		// log2_max_pic_order_cnt_lsb_minus4 ue(v)
		if (!_eg_skip_ev(buf, &bit_offset, size)) {
			return -1;
		}
	} else if (pic_order_cnt_type == 1) {
		// delta_pic_order_always_zero_flag u(1)
		if (!_eg_skip_u1(buf, &bit_offset, size)) {
			return -1;
		}

		// offset_for_non_ref_pic se(v)
		if (!_eg_skip_ev(buf, &bit_offset, size)) {
			return -1;
		}

		// offset_for_top_to_bottom_field se(v)
		if (!_eg_skip_ev(buf, &bit_offset, size)) {
			return -1;
		}

		// num_ref_frames_in_pic_order_cnt_cycle ue(v)
		u32 num_ref_frames_in_pic_order_cnt_cycle;
		if (!_eg_decode_uev(buf, &bit_offset, size, &num_ref_frames_in_pic_order_cnt_cycle)) {
			return -1;
		}

		for (u32 i = 0; i < num_ref_frames_in_pic_order_cnt_cycle; ++i) {
			// offset_for_ref_frame[i] se(v)
			if (!_eg_skip_ev(buf, &bit_offset, size)) {
				return -1;
			}
		}
	}

	// max_num_ref_frames ue(v)
	if (!_eg_skip_ev(buf, &bit_offset, size)) {
		return -1;
	}

	// gaps_in_frame_num_value_allowed_flag u(1)
	if (!_eg_skip_u1(buf, &bit_offset, size)) {
		return -1;
	}

	// pic_width_in_mbs_minus1 ue(v)
	u32 pic_width_in_mbs_minus1;
	if (!_eg_decode_uev(buf, &bit_offset, size, &pic_width_in_mbs_minus1)) {
		return -1;
	}

	//pic_height_in_map_units_minus1 ue(v)
	u32 pic_height_in_map_units_minus1;
	if (!_eg_decode_uev(buf, &bit_offset, size, &pic_height_in_map_units_minus1)) {
		return -1;
	}

	// frame_mbs_only_flag u(1)
	bool frame_mbs_only_flag;
	if (!_eg_decode_u1(buf, &bit_offset, size, &frame_mbs_only_flag)) {
		return -1;
	}

	if (!frame_mbs_only_flag) {
		// mb_adaptive_frame_field_flag u(1)
		if (!_eg_skip_u1(buf, &bit_offset, size)) {
			return -1;
		}
	}

	//direct_8x8_inference_flag u(1)
	if (!_eg_skip_u1(buf, &bit_offset, size)) {
		return -1;
	}

	// frame_cropping_flag u(1)
	bool frame_cropping_flag;
	if (!_eg_decode_u1(buf, &bit_offset, size, &frame_cropping_flag)) {
		return -1;
	}

	u32 frame_crop_left_offset = 0;
	u32 frame_crop_right_offset = 0;
	u32 frame_crop_top_offset = 0;
	u32 frame_crop_bottom_offset = 0;
	if (frame_cropping_flag) {
		// frame_crop_left_offset ue(v)
		if (!_eg_decode_uev(buf, &bit_offset, size, &frame_crop_left_offset)) {
			return -1;
		}

		// frame_crop_right_offset ue(v)
		if (!_eg_decode_uev(buf, &bit_offset, size, &frame_crop_right_offset)) {
			return -1;
		}

		// frame_crop_top_offset ue(v)
		if (!_eg_decode_uev(buf, &bit_offset, size, &frame_crop_top_offset)) {
			return -1;
		}

		// frame_crop_bottom_offset ue(v)
		if (!_eg_decode_uev(buf, &bit_offset, size, &frame_crop_bottom_offset)) {
			return -1;
		}
	}

	*width = (
		(pic_width_in_mbs_minus1 + 1) * 16
		- frame_crop_left_offset * 2
		- frame_crop_right_offset * 2
	);
	*height = (
		(pic_height_in_map_units_minus1 + 1) * (2 - frame_mbs_only_flag) * 16
		- frame_crop_top_offset * 2
		- frame_crop_bottom_offset * 2
	);
	return 0;
}
