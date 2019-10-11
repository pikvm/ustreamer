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


#include <assert.h>
#ifdef NDEBUG
#	error WTF dude? Asserts are good things!
#endif

#include <stdio.h>
#include <signal.h>

#include <pthread.h>

#include "tools.h"
#include "threading.h"
#include "logging.h"
#include "options.h"
#include "device.h"
#include "encoder.h"
#include "stream.h"
#include "http/server.h"
#ifdef WITH_GPIO
#	include "gpio.h"
#endif


struct _main_context_t {
	struct stream_t			*stream;
	struct http_server_t	*server;
};

static struct _main_context_t *_ctx;

static void _block_thread_signals(void) {
	sigset_t mask;
	assert(!sigemptyset(&mask));
	assert(!sigaddset(&mask, SIGINT));
	assert(!sigaddset(&mask, SIGTERM));
	assert(!pthread_sigmask(SIG_BLOCK, &mask, NULL));
}

static void *_stream_loop_thread(UNUSED void *arg) {
	A_THREAD_RENAME("stream");
	_block_thread_signals();
	stream_loop(_ctx->stream);
	return NULL;
}

static void *_server_loop_thread(UNUSED void *arg) {
	A_THREAD_RENAME("http");
	_block_thread_signals();
	http_server_loop(_ctx->server);
	return NULL;
}

static void _signal_handler(int signum) {
	LOG_INFO_NOLOCK("===== Stopping by %s =====", (signum == SIGTERM ? "SIGTERM" : "SIGINT"));
	stream_loop_break(_ctx->stream);
	http_server_loop_break(_ctx->server);
}

static void _install_signal_handlers(void) {
	struct sigaction sig_act;

	MEMSET_ZERO(sig_act);
	assert(!sigemptyset(&sig_act.sa_mask));
	sig_act.sa_handler = _signal_handler;
	assert(!sigaddset(&sig_act.sa_mask, SIGINT));
	assert(!sigaddset(&sig_act.sa_mask, SIGTERM));

	LOG_INFO("Installing SIGINT handler ...");
	assert(!sigaction(SIGINT, &sig_act, NULL));

	LOG_INFO("Installing SIGTERM handler ...");
	assert(!sigaction(SIGTERM, &sig_act, NULL));

	LOG_INFO("Ignoring SIGPIPE ...");
	assert(signal(SIGPIPE, SIG_IGN) != SIG_ERR);
}

int main(int argc, char *argv[]) {
	struct options_t *options;
	struct device_t *dev;
	struct encoder_t *encoder;
	struct stream_t *stream;
	struct http_server_t *server;
	int exit_code = 0;

	LOGGING_INIT;
	A_THREAD_RENAME("main");
	options = options_init(argc, argv);

#	ifdef WITH_GPIO
	GPIO_INIT;
#	endif

	dev = device_init();
	encoder = encoder_init();
	stream = stream_init(dev, encoder);
	server = http_server_init(stream);

	if ((exit_code = options_parse(options, dev, encoder, server)) == 0) {
#		ifdef WITH_GPIO
		GPIO_INIT_PINOUT;
#		endif

		_install_signal_handlers();

		pthread_t stream_loop_tid;
		pthread_t server_loop_tid;
		struct _main_context_t ctx;

		ctx.stream = stream;
		ctx.server = server;
		_ctx = &ctx;

		if ((exit_code = http_server_listen(server)) == 0) {
#			ifdef WITH_GPIO
			GPIO_SET_HIGH(prog_running);
#			endif

			A_THREAD_CREATE(&stream_loop_tid, _stream_loop_thread, NULL);
			A_THREAD_CREATE(&server_loop_tid, _server_loop_thread, NULL);
			A_THREAD_JOIN(server_loop_tid);
			A_THREAD_JOIN(stream_loop_tid);
		}
	}

	http_server_destroy(server);
	stream_destroy(stream);
	encoder_destroy(encoder);
	device_destroy(dev);

#	ifdef WITH_GPIO
	GPIO_SET_LOW(prog_running);
#	endif

	options_destroy(options);
	if (exit_code == 0) {
		LOG_INFO("Bye-bye");
	}
	LOGGING_DESTROY;
	return (exit_code < 0 ? 1 : 0);
}
