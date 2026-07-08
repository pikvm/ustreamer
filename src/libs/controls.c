/*****************************************************************************
#                                                                            #
#    uStreamer - Lightweight and fast MJPEG-HTTP streamer.                   #
#                                                                            #
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


#include "controls.h"

#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include <linux/videodev2.h>
#include <linux/v4l2-controls.h>

#include "types.h"
#include "tools.h"
#include "logging.h"
#include "xioctl.h"


static int _query_control(
	int fd,
	struct v4l2_queryctrl *query,
	const char *name,
	uint cid,
	bool quiet);

static void _set_control(
	int fd,
	const struct v4l2_queryctrl *query,
	const char *name,
	uint cid,
	int value,
	bool quiet);


#define _LOG_ERROR(x_msg, ...)	US_LOG_ERROR("CAP: " x_msg, ##__VA_ARGS__)
#define _LOG_PERROR(x_msg, ...)	US_LOG_PERROR("CAP: " x_msg, ##__VA_ARGS__)
#define _LOG_INFO(x_msg, ...)	US_LOG_INFO("CAP: " x_msg, ##__VA_ARGS__)


us_controls_s *us_controls_init(void) {
	us_controls_s *ctl;
	US_CALLOC(ctl, 1);
	return ctl;
}

void us_controls_destroy(us_controls_s *ctl) {
	free(ctl);
}

void us_controls_apply(const us_controls_s *ctl, int fd) {
#	define SET_CID_VALUE(x_cid, x_field, x_value, x_quiet) { \
			struct v4l2_queryctrl m_query; \
			if (_query_control(fd, &m_query, #x_field, x_cid, x_quiet) == 0) { \
				_set_control(fd, &m_query, #x_field, x_cid, x_value, x_quiet); \
			} \
		}

#	define SET_CID_DEFAULT(x_cid, x_field, x_quiet) { \
			struct v4l2_queryctrl m_query; \
			if (_query_control(fd, &m_query, #x_field, x_cid, x_quiet) == 0) { \
				_set_control(fd, &m_query, #x_field, x_cid, m_query.default_value, x_quiet); \
			} \
		}

#	define CONTROL_MANUAL_CID(x_cid, x_field) { \
			if (ctl->x_field.mode == US_CTL_MODE_VALUE) { \
				SET_CID_VALUE(x_cid, x_field, ctl->x_field.value, false); \
			} else if (ctl->x_field.mode == US_CTL_MODE_DEFAULT) { \
				SET_CID_DEFAULT(x_cid, x_field, false); \
			} \
		}

#	define CONTROL_AUTO_CID(x_cid_auto, x_cid_manual, x_field) { \
			if (ctl->x_field.mode == US_CTL_MODE_VALUE) { \
				SET_CID_VALUE(x_cid_auto, x_field##_auto, 0, true); \
				SET_CID_VALUE(x_cid_manual, x_field, ctl->x_field.value, false); \
			} else if (ctl->x_field.mode == US_CTL_MODE_AUTO) { \
				SET_CID_VALUE(x_cid_auto, x_field##_auto, 1, false); \
			} else if (ctl->x_field.mode == US_CTL_MODE_DEFAULT) { \
				SET_CID_VALUE(x_cid_auto, x_field##_auto, 0, true); /* Reset inactive flag */ \
				SET_CID_DEFAULT(x_cid_manual, x_field, false); \
				SET_CID_DEFAULT(x_cid_auto, x_field##_auto, false); \
			} \
		}

	CONTROL_AUTO_CID	(V4L2_CID_AUTOBRIGHTNESS,		V4L2_CID_BRIGHTNESS,				brightness);
	CONTROL_MANUAL_CID	(								V4L2_CID_CONTRAST,					contrast);
	CONTROL_MANUAL_CID	(								V4L2_CID_SATURATION,				saturation);
	CONTROL_AUTO_CID	(V4L2_CID_HUE_AUTO,				V4L2_CID_HUE,						hue);
	CONTROL_MANUAL_CID	(								V4L2_CID_GAMMA,						gamma);
	CONTROL_MANUAL_CID	(								V4L2_CID_SHARPNESS,					sharpness);
	CONTROL_MANUAL_CID	(								V4L2_CID_BACKLIGHT_COMPENSATION,	backlight_compensation);
	CONTROL_AUTO_CID	(V4L2_CID_AUTO_WHITE_BALANCE,	V4L2_CID_WHITE_BALANCE_TEMPERATURE,	white_balance);
	CONTROL_AUTO_CID	(V4L2_CID_AUTOGAIN,				V4L2_CID_GAIN,						gain);
	CONTROL_MANUAL_CID	(								V4L2_CID_COLORFX,					color_effect);
	CONTROL_MANUAL_CID	(								V4L2_CID_ROTATE,	   				rotate);
	CONTROL_MANUAL_CID	(								V4L2_CID_VFLIP,						flip_vertical);
	CONTROL_MANUAL_CID	(								V4L2_CID_HFLIP,						flip_horizontal);

#	undef CONTROL_AUTO_CID
#	undef CONTROL_MANUAL_CID
#	undef SET_CID_DEFAULT
#	undef SET_CID_VALUE
}

static int _query_control(
	int fd,
	struct v4l2_queryctrl *query,
	const char *name,
	uint cid,
	bool quiet
) {
	// cppcheck-suppress redundantPointerOp
	US_MEMSET_ZERO(*query);
	query->id = cid;

	if (us_xioctl(fd, VIDIOC_QUERYCTRL, query) < 0 || query->flags & V4L2_CTRL_FLAG_DISABLED) {
		if (!quiet) {
			_LOG_ERROR("Changing control %s is unsupported", name);
		}
		return -1;
	}
	return 0;
}

static void _set_control(
	int fd,
	const struct v4l2_queryctrl *query,
	const char *name,
	uint cid,
	int value,
	bool quiet
) {
	if (value < query->minimum || value > query->maximum || value % query->step != 0) {
		if (!quiet) {
			_LOG_ERROR("Invalid value %d of control %s: min=%d, max=%d, default=%d, step=%u",
				value, name, query->minimum, query->maximum, query->default_value, query->step);
		}
		return;
	}

	struct v4l2_control ctl = {
		.id = cid,
		.value = value,
	};
	if (us_xioctl(fd, VIDIOC_S_CTRL, &ctl) < 0) {
		if (!quiet) {
			_LOG_PERROR("Can't set control %s", name);
		}
	} else if (!quiet) {
		_LOG_INFO("Applying control %s: %d", name, ctl.value);
	}
}
