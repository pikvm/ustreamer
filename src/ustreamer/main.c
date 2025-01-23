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


#include <stdio.h>
#include <stdbool.h>

#include <pthread.h>

#include "../libs/tools.h"
#include "../libs/threading.h"
#include "../libs/logging.h"
#include "../libs/capture.h"
#include "../libs/signal.h"

#include "options.h"
#include "encoder.h"
#include "stream.h"
#include "http/server.h"
#ifdef WITH_GPIO
#	include "gpio/gpio.h"
#endif


static us_stream_s	*_g_stream = NULL;
static us_server_s	*_g_server = NULL;


static void _block_thread_signals(void) {
	sigset_t mask;
	assert(!sigemptyset(&mask));
	assert(!sigaddset(&mask, SIGINT));
	assert(!sigaddset(&mask, SIGTERM));
	assert(!pthread_sigmask(SIG_BLOCK, &mask, NULL));
}

static void *_stream_loop_thread(void *arg) {
	(void)arg;
	US_THREAD_SETTLE("stream");
	_block_thread_signals();
	us_stream_loop(_g_stream);
	return NULL;
}

static void *_server_loop_thread(void *arg) {
	(void)arg;
	US_THREAD_SETTLE("http");
	_block_thread_signals();
	us_server_loop(_g_server);
	return NULL;
}

static void _signal_handler(int signum) {
	char *const name = us_signum_to_string(signum);
	US_LOG_INFO_NOLOCK("===== Stopping by %s =====", name);
	free(name);
	us_stream_loop_break(_g_stream);
	us_server_loop_break(_g_server);
}

int main(int argc, char *argv[]) {
	assert(argc >= 0);
	int exit_code = 0;

	US_LOGGING_INIT;
	US_THREAD_RENAME("main");

	us_options_s *options = us_options_init(argc, argv);
	us_capture_s *cap = us_capture_init();
	us_encoder_s *enc = us_encoder_init();
	_g_stream = us_stream_init(cap, enc);
	_g_server = us_server_init(_g_stream);

	if ((exit_code = options_parse(options, cap, enc, _g_stream, _g_server)) == 0) {
		us_stream_update_blank(_g_stream, cap);
#		ifdef WITH_GPIO
		us_gpio_init();
#		endif

		us_install_signals_handler(_signal_handler, true);

		if ((exit_code = us_server_listen(_g_server)) == 0) {
#			ifdef WITH_GPIO
			us_gpio_set_prog_running(true);
#			endif

			pthread_t stream_loop_tid;
			pthread_t server_loop_tid;
			US_THREAD_CREATE(stream_loop_tid, _stream_loop_thread, NULL);
			US_THREAD_CREATE(server_loop_tid, _server_loop_thread, NULL);
			US_THREAD_JOIN(server_loop_tid);
			US_THREAD_JOIN(stream_loop_tid);
		}

#		ifdef WITH_GPIO
		us_gpio_set_prog_running(false);
		us_gpio_destroy();
#		endif
	}

	us_server_destroy(_g_server);
	us_stream_destroy(_g_stream);
	us_encoder_destroy(enc);
	us_capture_destroy(cap);
	us_options_destroy(options);

	if (exit_code == 0) {
		US_LOG_INFO("Bye-bye");
	}
	US_LOGGING_DESTROY;
	return (exit_code < 0 ? 1 : 0);
}
