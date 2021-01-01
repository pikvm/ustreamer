#include <stdio.h>
#include "../common/logging.h"
#include "../common/frame.h"
#include "../rawsink/rawsink.h"
#include "../h264/encoder.h"

int main(void) {
	LOGGING_INIT;
	log_level = 3;

	frame_s *src = frame_init("src");
	frame_s *dest = frame_init("dest");
	h264_encoder_s *encoder = h264_encoder_init();
	rawsink_s *rawsink = rawsink_init("test", false, 0, 0, (long double)encoder->fps / (long double)encoder->gop);
	FILE *fp = fopen("test.h264", "wb");

	if (rawsink) {
		int error = 0;
		while ((error = rawsink_client_get(rawsink, src)) != -1) {
			if (error == 0 /*|| (error == -2 && src->used > 0)*/) {
				if (!h264_encoder_compress(encoder, src, dest, false)) {
					LOG_INFO("frame %Lf", get_now_monotonic() - dest->grab_ts);
					/*fwrite(dest->data, 1, dest->used, fp);
					fflush(fp);*/
				}
			}
		}
	}
	return 0;
}
