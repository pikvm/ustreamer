#include <stdio.h>
#include <assert.h>

#include <bcm_host.h>

#include "../common/logging.h"
#include "../common/frame.h"
#include "../memsink/memsink.h"
#include "../h264/encoder.h"

int main(void) {
	LOGGING_INIT;
	log_level = 3;

	bcm_host_init();

	frame_s *src = frame_init("src");
	frame_s *dest = frame_init("dest");
	h264_encoder_s *encoder = h264_encoder_init();
	memsink_s *memsink = memsink_init("RAW", "test", false, 0, 0, (long double)encoder->fps / (long double)encoder->gop);
	assert(memsink);
	FILE *fp = fopen("test.h264", "wb");
	assert(fp);

	int error = 0;
	while ((error = memsink_client_get(memsink, src)) != -1) {
		if (error == 0 /*|| (error == -2 && src->used > 0)*/) {
			if (!h264_encoder_compress(encoder, src, dest, false)) {
				LOG_INFO("frame %Lf", get_now_monotonic() - dest->grab_ts);
				fwrite(dest->data, 1, dest->used, fp);
				fflush(fp);
			}
		}
	}
	fclose(fp);
	return 0;
}
