/*****************************************************************************
#    uStreamer - Lightweight and fast MJPG-HTTP streamer.                    #
#                                                                            #
#    Copyright (C) 2018  Maxim Devaev <mdevaev@gmail.com>                    #
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

#include <stdio.h>
#include <unistd.h>

#include <sys/types.h>
#include <sys/syscall.h>

#include <IL/OMX_IVCommon.h>
#include <IL/OMX_Core.h>
#include <IL/OMX_Image.h>

#include "../logging.h"
#include "../tools.h"


#define LOG_OMX_ERROR(_error, _msg, ...) { \
		LOGGING_LOCK; \
		printf("-- ERROR [%.03Lf tid=%ld] -- " _msg ": %s\n", now_ms_ld(), \
			syscall(SYS_gettid), ##__VA_ARGS__, omx_error_to_string(_error)); \
		LOGGING_UNLOCK; \
	}


const char *omx_error_to_string(const OMX_ERRORTYPE error);
const char *omx_state_to_string(const OMX_STATETYPE state);
