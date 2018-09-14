#include <string.h>
#include <errno.h>
#include <sys/ioctl.h>

#include "tools.h"


int xioctl(const int fd, const int request, void *arg) {
	int retries = XIOCTL_RETRIES;
	int retval = -1;

	do {
		retval = ioctl(fd, request, arg);
	} while (
		retval
		&& retries--
		&& (
			errno == EINTR
			|| errno == EAGAIN
			|| errno == ETIMEDOUT
		)
	);

	if (retval && retries <= 0) {
		LOG_PERROR("ioctl(%d) retried %d times; giving up", request, XIOCTL_RETRIES);
	}
	return retval;
}
