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

#include <signal.h>
#include <unistd.h>

#if defined(__FreeBSD__)
#	include <sys/param.h>
#endif


#ifdef WITH_SETPROCTITLE
#	include <stdlib.h>
#	include <string.h>
#	if defined(__linux__)
#		include <bsd/unistd.h>
#	elif (defined(__FreeBSD__) || defined(__DragonFly__))
//#		include <unistd.h>
//#		include <sys/types.h>
#	elif (defined(__NetBSD__) || defined(__OpenBSD__)) // setproctitle() placed in stdlib.h
#	else
#		error setproctitle() not implemented, you can disable it using WITH_SETPROCTITLE=0
#	endif
#endif

#ifdef WITH_PDEATHSIG
#	if defined(__linux__)
#		include <sys/prctl.h>
#	elif defined(__FreeBSD__) && (__FreeBSD_version >= 1102000)
#		include <sys/procctl.h>
#	else
#		error WITH_PDEATHSIG is not supported on your system
#	endif
#endif

#include "types.h"
#ifdef WITH_SETPROCTITLE
#	include "tools.h"
#endif
#include "logging.h"


#ifdef WITH_SETPROCTITLE
extern char **environ;
#endif


#ifdef WITH_PDEATHSIG
INLINE int us_process_track_parent_death(void) {
	const pid_t parent = getppid();
	int signum = SIGTERM;
#	if defined(__linux__)
	const int retval = prctl(PR_SET_PDEATHSIG, signum);
#	elif defined(__FreeBSD__)
	const int retval = procctl(P_PID, 0, PROC_PDEATHSIG_CTL, &signum);
#	else
#		error WTF?
#	endif
	if (retval < 0) {
		US_LOG_PERROR("Can't set to receive SIGTERM on parent process death");
		return -1;
	}

	if (kill(parent, 0) < 0) {
		US_LOG_PERROR("The parent process %d is already dead", parent);
		return -1;
	}
	return 0;
}
#endif

#ifdef WITH_SETPROCTITLE
#	pragma GCC diagnostic ignored "-Wunused-parameter"
#	pragma GCC diagnostic push
INLINE void us_process_set_name_prefix(int argc, char *argv[], const char *prefix) { // cppcheck-suppress constParameter
#	pragma GCC diagnostic pop

	char *cmdline = NULL;
	uz allocated = 2048;
	uz used = 0;

	US_REALLOC(cmdline, allocated);
	cmdline[0] = '\0';

	for (int index = 0; index < argc; ++index) {
		uz arg_len = strlen(argv[index]);
		if (used + arg_len + 16 >= allocated) {
			allocated += arg_len + 2048;
			US_REALLOC(cmdline, allocated); // cppcheck-suppress memleakOnRealloc // False-positive (ok with assert)
		}

		strcat(cmdline, " ");
		strcat(cmdline, argv[index]);
		used = strlen(cmdline); // Не считаем вручную, так надежнее
	}

#	ifdef __linux__
	setproctitle_init(argc, argv, environ);
#	endif
	setproctitle("-%s:%s", prefix, cmdline);

	free(cmdline);
}
#endif

INLINE void us_process_notify_parent(void) {
	const pid_t parent = getppid();
	if (kill(parent, SIGUSR2) < 0) {
		US_LOG_PERROR("Can't send SIGUSR2 to the parent process %d", parent);
	}
}

INLINE void us_process_suicide(void) {
	const pid_t pid = getpid();
	if (kill(pid, SIGTERM) < 0) {
		US_LOG_PERROR("Can't send SIGTERM to own pid %d", pid);
	}
}
