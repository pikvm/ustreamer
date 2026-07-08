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


#pragma once

#include "types.h"


typedef enum {
	US_CTL_MODE_NONE = 0,
	US_CTL_MODE_VALUE,
	US_CTL_MODE_AUTO,
	US_CTL_MODE_DEFAULT,
} us_control_mode_e;

typedef struct {
	us_control_mode_e	mode;
	int					value;
} us_control_s;

typedef struct {
	us_control_s	brightness;
	us_control_s	contrast;
	us_control_s	saturation;
	us_control_s	hue;
	us_control_s	gamma;
	us_control_s	sharpness;
	us_control_s	backlight_compensation;
	us_control_s	white_balance;
	us_control_s	gain;
	us_control_s	color_effect;
	us_control_s	rotate;
	us_control_s	flip_vertical;
	us_control_s	flip_horizontal;
} us_controls_s;


us_controls_s *us_controls_init(void);
void us_controls_destroy(us_controls_s *ctl);

void us_controls_apply(const us_controls_s *ctl, int fd);
