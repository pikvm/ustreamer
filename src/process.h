/*****************************************************************************
#                                                                            #
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

#if defined(__linux__)
#	define HAS_PDEATHSIG
#elif defined(__FreeBSD__)
#	include <sys/param.h>
#	if __FreeBSD_version >= 1102000
#		define HAS_PDEATHSIG
#	endif
#endif

#ifdef HAS_PDEATHSIG
#	include <signal.h>
#	include <unistd.h>

#	if defined(__linux__)
#		include <sys/prctl.h>
#	elif defined(__FreeBSD__)
#		include <sys/procctl.h>
#	endif

#	include "logging.h"


INLINE int process_track_parent_death(void) {
	int signum = SIGTERM;
#	if defined(__linux__)
	int retval = prctl(PR_SET_PDEATHSIG, signum);
#	elif defined(__FreeBSD__)
	int retval = procctl(P_PID, 0, PROC_PDEATHSIG_CTL, &signum);
#	else
#		error WTF?
#	endif
	if (retval < 0) {
		LOG_PERROR("Can't set to receive SIGTERM on parent process death");
		return -1;
	}

	if (kill(getppid(), 0) < 0) {
		LOG_PERROR("The parent process is already dead");
		return -1;
	}

	return 0;
}

#endif // HAS_PDEATHSIG
