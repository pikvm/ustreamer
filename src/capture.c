#include <unistd.h>
#include <errno.h>
#include <sys/select.h>
#include <linux/videodev2.h>

#include "tools.h"
#include "device.h"
#include "jpeg.h"


static int _capture_init_loop(struct device *dev);
static int _capture_init(struct device *dev);
static int _capture_control(struct device *dev, const bool enable);
static int _capture_grab_buffer(struct device *dev, struct v4l2_buffer *buf);
static int _capture_release_buffer(struct device *dev, struct v4l2_buffer *buf);
static int _capture_handle_event(struct device *dev);


void capture_loop(struct device *dev) {
	LOG_INFO("Using V4L2 device: %s", dev->path);
	LOG_INFO("Using JPEG quality: %d%%", dev->jpeg_quality);

	while (_capture_init_loop(dev) == 0) {
		int frames_count = 0;

		while (!(*dev->stop)) {
			SEP_DEBUG('-');

			fd_set read_fds;
			fd_set write_fds;
			fd_set error_fds;

			FD_ZERO(&read_fds);
			FD_SET(dev->run->fd, &read_fds);

			FD_ZERO(&write_fds);
			FD_SET(dev->run->fd, &write_fds);

			FD_ZERO(&error_fds);
			FD_SET(dev->run->fd, &error_fds);

			struct timeval timeout;
			timeout.tv_sec = dev->timeout;
			timeout.tv_usec = 0;

			LOG_DEBUG("Calling select() on video device ...");
			int retval = select(dev->run->fd + 1, &read_fds, &write_fds, &error_fds, &timeout);
			LOG_DEBUG("Device select() --> %d", retval);

			if (retval < 0) {
				if (errno != EINTR) {
					LOG_PERROR("Mainloop select() error");
					break;
				}

			} else if (retval == 0) {
				LOG_ERROR("Mainloop select() timeout");
				break;

			} else {
				if (FD_ISSET(dev->run->fd, &read_fds)) {
					LOG_DEBUG("Frame ready ...");

					struct v4l2_buffer buf;

					if (_capture_grab_buffer(dev, &buf) < 0) {
						break;
					}

					if (dev->every_frame) {
						if (frames_count < (int)dev->every_frame - 1) {
							LOG_DEBUG("Dropping frame %d for option --every-frame=%d", frames_count + 1, dev->every_frame);
							++frames_count;
							goto pass_frame;
						} else {
							frames_count = 0;
						}
					}

					// Workaround for broken, corrupted frames:
					// Under low light conditions corrupted frames may get captured.
					// The good thing is such frames are quite small compared to the regular pictures.
					// For example a VGA (640x480) webcam picture is normally >= 8kByte large,
					// corrupted frames are smaller.
					if (buf.bytesused < dev->min_frame_size) {
						LOG_DEBUG("Dropping too small frame sized %d bytes, assuming it as broken", buf.bytesused);
						goto pass_frame;
					}

					LOG_DEBUG("Grabbed a new frame");
					jpeg_compress_buffer(dev, buf.index);
					//usleep(100000); // TODO: process dev->run->buffers[buf.index].start, buf.bytesused

					pass_frame:

					if (_capture_release_buffer(dev, &buf) < 0) {
						break;
					}
				}

				if (FD_ISSET(dev->run->fd, &write_fds)) {
					LOG_ERROR("Got unexpected writing event, seems device was disconnected");
					break;
				}

				if (FD_ISSET(dev->run->fd, &error_fds)) {
					LOG_INFO("Got V4L2 event");
					if (_capture_handle_event(dev) < 0) {
						break;
					}
				}
			}
		}
	}
	_capture_control(dev, false);
	device_close(dev);
}

static int _capture_init_loop(struct device *dev) {
	int retval = -1;

	while (!(*dev->stop)) {
		if ((retval = _capture_init(dev)) < 0) {
			LOG_INFO("Sleeping %d seconds before new capture init ...", dev->error_timeout);
			sleep(dev->error_timeout);
		} else {
			break;
		}
	}
	return retval;
}

static int _capture_init(struct device *dev) {
	SEP_INFO('=');

	_capture_control(dev, false);
	device_close(dev);

	if (device_open(dev) < 0) {
		goto error;
	}
	if (_capture_control(dev, true) < 0) {
		goto error;
	}

	return 0;

	error:
		device_close(dev);
		return -1;
}

static int _capture_control(struct device *dev, const bool enable) {
	if (enable != dev->run->capturing) {
		enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

		LOG_DEBUG("Calling ioctl(%s) ...", (enable ? "VIDIOC_STREAMON" : "VIDIOC_STREAMOFF"));
		if (xioctl(dev->run->fd, (enable ? VIDIOC_STREAMON : VIDIOC_STREAMOFF), &type) < 0) {
			LOG_PERROR("Unable to %s capturing", (enable ? "start" : "stop"));
			if (enable) {
				return -1;
			}
		}

		dev->run->capturing = enable;
		LOG_INFO("Capturing %s", (enable ? "started" : "stopped"));
	}
    return 0;
}

static int _capture_grab_buffer(struct device *dev, struct v4l2_buffer *buf) {
	memset(buf, 0, sizeof(struct v4l2_buffer));
	buf->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	buf->memory = V4L2_MEMORY_MMAP;

	LOG_DEBUG("Calling ioctl(VIDIOC_DQBUF) ...");
	if (xioctl(dev->run->fd, VIDIOC_DQBUF, buf) < 0) {
		LOG_PERROR("Unable to dequeue buffer");
		return -1;
	}

	LOG_DEBUG("Got a new frame in buffer index=%d; bytesused=%d", buf->index, buf->bytesused);
	if (buf->index >= dev->run->n_buffers) {
		LOG_ERROR("Got invalid buffer index=%d; nbuffers=%d", buf->index, dev->run->n_buffers);
		return -1;
	}
	return 0;
}

static int _capture_release_buffer(struct device *dev, struct v4l2_buffer *buf) {
	LOG_DEBUG("Calling ioctl(VIDIOC_QBUF) ...");
	if (xioctl(dev->run->fd, VIDIOC_QBUF, buf) < 0) {
		LOG_PERROR("Unable to requeue buffer");
		return -1;
	}
	return 0;
}

static int _capture_handle_event(struct device *dev) {
	struct v4l2_event event;

	LOG_DEBUG("Calling ioctl(VIDIOC_DQEVENT) ...");
	if (!xioctl(dev->run->fd, VIDIOC_DQEVENT, &event)) {
		switch (event.type) {
			case V4L2_EVENT_SOURCE_CHANGE:
				LOG_INFO("Got V4L2_EVENT_SOURCE_CHANGE: source changed");
				return -1;
			case V4L2_EVENT_EOS:
				LOG_INFO("Got V4L2_EVENT_EOS: end of stream (ignored)");
				return 0;
		}
	} else {
		LOG_ERROR("Got some V4L2 device event, but where is it? ");
	}
	return 0;
}
