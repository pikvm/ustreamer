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


#include "memsink.h"

#include <stdatomic.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>

#include <sys/file.h>
#include <sys/stat.h>
#include <sys/mman.h>

#include "types.h"
#include "errors.h"
#include "tools.h"
#include "logging.h"
#include "frame.h"
#include "memsinksh.h"


us_memsink_s *us_memsink_init_opened(
	const char *name, const char *obj, bool server,
	mode_t mode, bool rm, uint client_ttl, uint timeout
) {
	us_memsink_s *sink;
	US_CALLOC(sink, 1);
	sink->name = name;
	sink->obj = obj;
	sink->server = server;
	sink->rm = rm;
	sink->client_ttl = client_ttl;
	sink->timeout = timeout;
	sink->fd = -1;
	atomic_init(&sink->has_clients, false);

	US_LOG_INFO("Using %s-sink: %s", name, obj);

	if ((sink->data_size = us_memsinksh_calculate_size(obj)) == 0) {
		US_LOG_ERROR("%s-sink: Invalid object suffix", name);
		goto error;
	}

	const mode_t mask = umask(0);
	sink->fd = shm_open(sink->obj, (server ? O_RDWR | O_CREAT : O_RDWR), mode);
	umask(mask);
	if (sink->fd == -1) {
		umask(mask);
		US_LOG_PERROR("%s-sink: Can't open shared memory", name);
		goto error;
	}

	if (sink->server && ftruncate(sink->fd, sizeof(us_memsink_shared_s) + sink->data_size) < 0) {
		US_LOG_PERROR("%s-sink: Can't truncate shared memory", name);
		goto error;
	}

	if ((sink->mem = us_memsinksh_map(sink->fd, sink->data_size)) == NULL) {
		US_LOG_PERROR("%s-sink: Can't mmap shared memory", name);
		goto error;
	}
	return sink;

error:
	us_memsink_destroy(sink);
	return NULL;
}

void us_memsink_destroy(us_memsink_s *sink) {
	if (sink->mem != NULL) {
		if (us_memsinksh_unmap(sink->mem, sink->data_size) < 0) {
			US_LOG_PERROR("%s-sink: Can't unmap shared memory", sink->name);
		}
	}
	if (sink->fd >= 0) {
		if (close(sink->fd) < 0) {
			US_LOG_PERROR("%s-sink: Can't close shared memory fd", sink->name);
		}
		if (sink->rm && shm_unlink(sink->obj) < 0) {
			if (errno != ENOENT) {
				US_LOG_PERROR("%s-sink: Can't remove shared memory", sink->name);
			}
		}
	}
	free(sink);
}

bool us_memsink_server_check(us_memsink_s *sink, const us_frame_s *frame) {
	// Если frame == NULL, то только проверяем наличие клиентов
	// или необходимость инициализировать память.

	US_A(sink->server);

	if (sink->mem->magic != US_MEMSINK_MAGIC || sink->mem->version != US_MEMSINK_VERSION) {
		// Если регион памяти не был инициализирован, то нужно что-то туда положить.
		// Блокировка не нужна, потому что только сервер пишет в эти переменные.
		return true;
	}

	const ldf unsafe_ts = sink->mem->last_client_ts;
	if (unsafe_ts != sink->unsafe_last_client_ts) {
		// Клиент пишет в синке свою отметку last_client_ts при любом действии.
		// Мы не берем блокировку здесь, а просто проверяем, является ли это число тем же самым,
		// что было прочитано нами в предыдущих итерациях. Значению не нужно быть консистентным,
		// и даже если мы прочитали мусор из-за гонки в памяти между чтением здеси и записью
		// из клиента, мы все равно можем сделать вывод, есть ли у нас клиенты вообще.
		// Если число число поменялось то у нас точно есть клиенты и дальнейшие проверки
		// проводить не требуется. Если же число неизменно, то стоит поставить блокировку
		// и проверить, нужно ли записать что-нибудь в память для инициализации фрейма.
		sink->unsafe_last_client_ts = unsafe_ts;
		atomic_store(&sink->has_clients, true);
		return true;
	}

	if (flock(sink->fd, LOCK_EX | LOCK_NB) < 0) {
		if (errno == EWOULDBLOCK) {
			// Есть живой клиент, который прямо сейчас взял блокировку и читает фрейм из синка
			atomic_store(&sink->has_clients, true);
			return true;
		}
		US_LOG_PERROR("%s-sink: Can't lock memory", sink->name);
		return false;
	}

	// Проверяем, есть ли у нас живой клиент по таймауту
	const bool has_clients = us_memsinksh_has_clients(sink->mem, sink->client_ttl);
	atomic_store(&sink->has_clients, has_clients);

	if (us_memsink_server_x_unlock(sink) < 0) {
		return false;
	}
	if (has_clients) {
		return true;
	}
	if (frame != NULL && !US_FRAME_COMPARE_GEOMETRY(sink->mem, frame)) {
		// Если есть изменения в геометрии/формате фрейма, то их тоже нобходимо сразу записать в синк
		return true;
	}
	return false;
}

int us_memsink_server_put(
	us_memsink_s *sink,
	const us_frame_s *frame,
	us_memsink_wants_s *w_get
) {
	US_A(sink->server);

	const ldf now = us_get_now_monotonic();

	if (us_flock_timedwait_monotonic(sink->fd, 1) == 0) {
		const bool has_clients = us_memsinksh_has_clients(sink->mem, sink->client_ttl);
		atomic_store(&sink->has_clients, has_clients);

		US_LOG_VERBOSE("%s-sink: >>>>> Exposing new frame ...", sink->name);
		const bool exposed = !us_memsink_server_x_put(sink, frame);

		if (w_get != NULL) {
			if (has_clients) {
				memcpy(w_get, &sink->mem->wants, sizeof(us_memsink_wants_s));
			} else {
				US_MEMSET_ZERO(*w_get);
			}
		}

		if (us_memsink_server_x_unlock(sink) < 0) {
			return -1;
		}
		if (exposed) {
			US_LOG_VERBOSE("%s-sink: Exposed new frame; full exposition time = %.3Lf",
				sink->name, us_get_now_monotonic() - now);
		}

	} else if (errno == EWOULDBLOCK) {
		US_LOG_VERBOSE("%s-sink: ===== Shared memory is busy now; frame skipped", sink->name);

	} else {
		US_LOG_PERROR("%s-sink: Can't lock memory", sink->name);
		return -1;
	}
	return 0;
}

int us_memsink_client_get(
	us_memsink_s *sink,
	us_frame_s *frame,
	us_memsink_wants_s *w_get,
	const us_memsink_wants_s *w_put
) {
	US_A(!sink->server); // Client only

	if (us_flock_timedwait_monotonic(sink->fd, sink->timeout) < 0) {
		if (errno == EWOULDBLOCK) {
			return US_ERROR_NO_DATA;
		}
		US_LOG_PERROR("%s-sink: Can't lock memory", sink->name);
		return -1;
	}

	int retval = 0;

	if (sink->mem->magic != US_MEMSINK_MAGIC) {
		retval = US_ERROR_NO_DATA; // Not updated
		goto done;
	}
	if (sink->mem->version != US_MEMSINK_VERSION) {
		US_LOG_ERROR("%s-sink: Protocol version mismatch: sink=%u, required=%u",
			sink->name, sink->mem->version, US_MEMSINK_VERSION);
		retval = -1;
		goto done;
	}

	const bool had_client_magic = (sink->mem->client_magic == US_MEMSINK_MAGIC);
	// Let the sink know that the client is alive
	sink->mem->last_client_ts = us_get_now_monotonic();
	sink->mem->client_magic = US_MEMSINK_MAGIC;

	if (sink->mem->used == 0 || sink->mem->id == sink->last_readed_id) {
		// ^ Zero frame     |OR| ^ Not updated
		retval = US_ERROR_NO_DATA;
		goto done;
	}

	sink->last_readed_id = sink->mem->id;
	us_frame_set_data(frame, us_memsinksh_get_data(sink->mem), sink->mem->used);
	US_FRAME_COPY_META(sink->mem, frame);

	if (w_get != NULL) {
		if (had_client_magic) { // Чтобы не прочитать мусор, если не было клиентов
			memcpy(w_get, &sink->mem->wants, sizeof(us_memsink_wants_s));
		} else {
			US_MEMSET_ZERO(*w_get);
		}
	}

	if (w_put != NULL) {
		const bool key = sink->mem->wants.key;
		memcpy(&sink->mem->wants, w_put, sizeof(us_memsink_wants_s));
		if (key) {
			sink->mem->wants.key = key;
		}
	} else {
		US_MEMSET_ZERO(sink->mem->wants);
	}

done:
	if (us_memsink_server_x_unlock(sink) < 0) {
		retval = -1;
	}
	return retval;
}

us_mss_lock_result_e us_memsink_server_x_lock(
	us_memsink_s *sink,
	us_memsink_wants_s *w_get
) {
	US_A(sink->server);

	if (us_flock_timedwait_monotonic(sink->fd, 1) < 0) {
		if (errno == EWOULDBLOCK) {
			return US_MSS_BUSY;
		}
		US_LOG_PERROR("%s-sink: Can't lock memory", sink->name);
		return US_MSS_ERROR;
	}

	if (sink->mem->magic == US_MEMSINK_MAGIC && sink->mem->version == US_MEMSINK_VERSION) {
		// Если инициализировано - значит клиенту что-то уже могли написать
		const bool has_clients = us_memsinksh_has_clients(sink->mem, sink->client_ttl);
		atomic_store(&sink->has_clients, has_clients);
		if (has_clients) {
			if (w_get != NULL) {
				memcpy(w_get, &sink->mem->wants, sizeof(us_memsink_wants_s));
			}
			// sink->unsafe_last_client_ts = sink->mem->last_client_ts; // _x_ functions don't use it
			return US_MSS_SUCCESS;
		}
	} else {
		// А иначе скормим клиентам инициализацию с пустым фреймом,
		// в следующий раз прочтем от них что-то путное.
		us_memsink_server_x_put(sink, NULL);
	}

	if (us_memsink_server_x_unlock(sink) < 0) {
		return US_MSS_ERROR;
	}
	return US_MSS_NO_CLIENT;
}

bool us_memsink_server_x_is_consumed(us_memsink_s *sink) {
	return (sink->mem->used == 0);
}

void us_memsink_server_x_set_consumed(us_memsink_s *sink) {
	sink->mem->used = 0;
}

int us_memsink_server_x_put(us_memsink_s *sink, const us_frame_s *frame) {
	if (frame != NULL) {
		if (frame->used > sink->data_size) {
			US_LOG_ERROR("%s-sink: Can't put frame: it's too big (%zu > %zu)",
				sink->name, frame->used, sink->data_size);
			return -1;
		}

		memcpy(us_memsinksh_get_data(sink->mem), frame->data, frame->used);
		sink->mem->used = frame->used;
		US_FRAME_COPY_META(frame, sink->mem);

		if (sink->mem->wants.key && frame->key) {
			// Можем писать в мусор, но пофигу
			sink->mem->wants.key = false;
		}
	} else {
		sink->mem->used = 0;
	}

	sink->mem->id = us_get_now_id();
	sink->mem->version = US_MEMSINK_VERSION;
	sink->mem->magic = US_MEMSINK_MAGIC;
	return 0;
}

int us_memsink_server_x_unlock(us_memsink_s *sink) {
	if (flock(sink->fd, LOCK_UN) < 0) {
		US_LOG_PERROR("%s-sink: Can't unlock memory", sink->name);
		return -1;
	}
	return 0;
}
