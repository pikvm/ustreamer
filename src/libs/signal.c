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


#include "signal.h"

#include <string.h>
#include <signal.h>
#include <assert.h>

#if defined(__GLIBC__) && __GLIBC__ >= 2 && __GLIBC_MINOR__ >= 32
#	define HAS_SIGABBREV_NP
#endif

#include "types.h"
#include "tools.h"
#include "logging.h"


char *us_signum_to_string(int signum) {
#	ifdef HAS_SIGABBREV_NP
	const char *const name = sigabbrev_np(signum);
#	else
	const char *const name = (
		signum == SIGTERM ? "TERM" :
		signum == SIGINT ? "INT" :
		signum == SIGPIPE ? "PIPE" :
		NULL
	);
#	endif
	char *buf;
	if (name != NULL) {
		US_ASPRINTF(buf, "SIG%s", name);
	} else {
		US_ASPRINTF(buf, "SIG[%d]", signum);
	}
	return buf;
}

void us_install_signals_handler(us_signal_handler_f handler, bool ignore_sigpipe) {
	struct sigaction sig_act = {0};

	assert(!sigemptyset(&sig_act.sa_mask));
	sig_act.sa_handler = handler;
	assert(!sigaddset(&sig_act.sa_mask, SIGINT));
	assert(!sigaddset(&sig_act.sa_mask, SIGTERM));
	if (!ignore_sigpipe) {
	    assert(!sigaddset(&sig_act.sa_mask, SIGPIPE));
	}

	US_LOG_DEBUG("Installing SIGINT handler ...");
	assert(!sigaction(SIGINT, &sig_act, NULL));

	US_LOG_DEBUG("Installing SIGTERM handler ...");
	assert(!sigaction(SIGTERM, &sig_act, NULL));

	if (!ignore_sigpipe) {
		US_LOG_DEBUG("Installing SIGPIPE handler ...");
		assert(!sigaction(SIGPIPE, &sig_act, NULL));
	} else {
		US_LOG_DEBUG("Ignoring SIGPIPE ...");
		assert(signal(SIGPIPE, SIG_IGN) != SIG_ERR);
	}
}
