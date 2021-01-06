#include <stdio.h>
#include <assert.h>

#include "../libs/logging.h"
#include "../libs/frame.h"
#include "../libs/memsink.h"

int main(void) {
	LOGGING_INIT;
	log_level = 3;

	frame_s *frame = frame_init("h264");
	memsink_s *sink = memsink_init("h264", "test", false, 0, 0, 0.1);
	assert(sink);
	FILE *fp = fopen("test.h264", "wb");
	assert(fp);

	int error = 0;
	while ((error = memsink_client_get(sink, frame)) != -1) {
		if (error == 0 /*|| (error == -2 && frame->used > 0)*/) {
			LOG_INFO("frame %Lf", get_now_monotonic() - frame->grab_ts);
			fwrite(frame->data, 1, frame->used, fp);
			fflush(fp);
		}
	}
	fclose(fp);
	return 0;
}
