/*****************************************************************************
#                                                                            #
#    uStreamer - Lightweight and fast MJPEG-HTTP streamer.                   #
#                                                                            #
#    Copyright (C) 2018-2023  Maxim Devaev <mdevaev@gmail.com>               #
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


us_memsink_s *us_memsink_init(
	const char *name, const char *obj, bool server,
	mode_t mode, bool rm, unsigned client_ttl, unsigned timeout) {

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

	const mode_t mask = umask(0);
	sink->fd = shm_open(sink->obj, (server ? O_RDWR | O_CREAT : O_RDWR), mode);
	umask(mask);
	if (sink->fd == -1) {
		umask(mask);
		US_LOG_PERROR("%s-sink: Can't open shared memory", name);
		goto error;
	}

	if (sink->server && ftruncate(sink->fd, sizeof(us_memsink_shared_s)) < 0) {
		US_LOG_PERROR("%s-sink: Can't truncate shared memory", name);
		goto error;
	}

	if ((sink->mem = us_memsink_shared_map(sink->fd)) == NULL) {
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
		if (us_memsink_shared_unmap(sink->mem) < 0) {
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
	// Return true (the need to write to memsink) on any of these conditions:
	//   - EWOULDBLOCK - we have an active client;
	//   - Incorrect magic or version - need to first write;
	//   - We have some active clients by last_client_ts;
	//   - Frame meta differs (like size, format, but not timestamp).

	assert(sink->server);

	if (flock(sink->fd, LOCK_EX | LOCK_NB) < 0) {
		if (errno == EWOULDBLOCK) {
			atomic_store(&sink->has_clients, true);
			return true;
		}
		US_LOG_PERROR("%s-sink: Can't lock memory", sink->name);
		return false;
	}

	if (sink->mem->magic != US_MEMSINK_MAGIC || sink->mem->version != US_MEMSINK_VERSION) {
		return true;
	}

	const bool has_clients = (sink->mem->last_client_ts + sink->client_ttl > us_get_now_monotonic());
	atomic_store(&sink->has_clients, has_clients);

	if (flock(sink->fd, LOCK_UN) < 0) {
		US_LOG_PERROR("%s-sink: Can't unlock memory", sink->name);
		return false;
	}
	return (has_clients || !US_FRAME_COMPARE_META_USED_NOTS(sink->mem, frame));;
}

int us_memsink_server_put(us_memsink_s *sink, const us_frame_s *frame, bool *const key_requested) {
	assert(sink->server);

	const long double now = us_get_now_monotonic();

	if (frame->used > US_MEMSINK_MAX_DATA) {
		US_LOG_ERROR("%s-sink: Can't put frame: is too big (%zu > %zu)",
			sink->name, frame->used, US_MEMSINK_MAX_DATA);
		return 0; // -2
	}

	if (us_flock_timedwait_monotonic(sink->fd, 1) == 0) {
		US_LOG_VERBOSE("%s-sink: >>>>> Exposing new frame ...", sink->name);

		sink->last_id = us_get_now_id();
		sink->mem->id = sink->last_id;
		if (sink->mem->key_requested && frame->key) {
			sink->mem->key_requested = false;
		}
		*key_requested = sink->mem->key_requested;

		memcpy(sink->mem->data, frame->data, frame->used);
		sink->mem->used = frame->used;
		US_FRAME_COPY_META(frame, sink->mem);

		sink->mem->magic = US_MEMSINK_MAGIC;
		sink->mem->version = US_MEMSINK_VERSION;

		atomic_store(&sink->has_clients, (sink->mem->last_client_ts + sink->client_ttl > us_get_now_monotonic()));

		if (flock(sink->fd, LOCK_UN) < 0) {
			US_LOG_PERROR("%s-sink: Can't unlock memory", sink->name);
			return -1;
		}
		US_LOG_VERBOSE("%s-sink: Exposed new frame; full exposition time = %.3Lf",
			sink->name, us_get_now_monotonic() - now);

	} else if (errno == EWOULDBLOCK) {
		US_LOG_VERBOSE("%s-sink: ===== Shared memory is busy now; frame skipped", sink->name);

	} else {
		US_LOG_PERROR("%s-sink: Can't lock memory", sink->name);
		return -1;
	}
	return 0;
}

int us_memsink_client_get(us_memsink_s *sink, us_frame_s *frame, bool *const key_requested, bool key_required) { // cppcheck-suppress unusedFunction
	assert(!sink->server); // Client only

	if (us_flock_timedwait_monotonic(sink->fd, sink->timeout) < 0) {
		if (errno == EWOULDBLOCK) {
			return -2;
		}
		US_LOG_PERROR("%s-sink: Can't lock memory", sink->name);
		return -1;
	}

	int retval = -2; // Not updated
	if (sink->mem->magic == US_MEMSINK_MAGIC) {
		if (sink->mem->version != US_MEMSINK_VERSION) {
			US_LOG_ERROR("%s-sink: Protocol version mismatch: sink=%u, required=%u",
				sink->name, sink->mem->version, US_MEMSINK_VERSION);
			retval = -1;
			goto done;
		}
		if (sink->mem->id != sink->last_id) { // When updated
			sink->last_id = sink->mem->id;
			us_frame_set_data(frame, sink->mem->data, sink->mem->used);
			US_FRAME_COPY_META(sink->mem, frame);
			*key_requested = sink->mem->key_requested;
			retval = 0;
		}
		sink->mem->last_client_ts = us_get_now_monotonic();
		if (key_required) {
			sink->mem->key_requested = true;
		}
	}

	done:
		if (flock(sink->fd, LOCK_UN) < 0) {
			US_LOG_PERROR("%s-sink: Can't unlock memory", sink->name);
			return -1;
		}
		return retval;
}
