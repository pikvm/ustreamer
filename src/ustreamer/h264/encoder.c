/*****************************************************************************
#                                                                            #
#    uStreamer - Lightweight and fast MJPG-HTTP streamer.                    #
#                                                                            #
#    Copyright (C) 2018-2021  Maxim Devaev <mdevaev@gmail.com>               #
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

#include "encoder.h"

#include <fcntl.h>

#include <sys/ioctl.h>
#include <sys/mman.h>

static void _h264_encoder_cleanup(h264_encoder_s *enc);
static int _h264_encoder_compress_raw(h264_encoder_s *enc, const frame_s *src,
                                      int src_vcsm_handle, frame_s *dest,
                                      bool force_key);

void map(int fd, __uint32_t type, buffer *buffers, int buffer_len) {
  for (int i = 0; i < buffer_len; ++i) {
    struct v4l2_buffer *inner = &buffers[i].inner;

    memset(inner, 0, sizeof(*inner));
    inner->type = type;
    inner->memory = V4L2_MEMORY_MMAP;
    inner->index = i;
    inner->length = 1;
    inner->m.planes = &buffers[i].planes;

    if (ioctl(fd, VIDIOC_QUERYBUF, inner) < 0) {
      perror("VIDIOC_QUERYBUF");
      exit(EXIT_FAILURE);
    }

    buffers[i].length = inner->m.planes[0].length;
    buffers[i].start =
        mmap(NULL, inner->m.planes[0].length, PROT_READ | PROT_WRITE,
             MAP_SHARED, fd, inner->m.planes[0].m.mem_offset);

    if (MAP_FAILED == buffers[i].start) {
      perror("mmap");
      exit(EXIT_FAILURE);
    }
  }
}

h264_encoder_s *h264_encoder_init(unsigned bitrate, unsigned gop,
                                  unsigned fps) {
  h264_encoder_s *enc;
  A_CALLOC(enc, 1);

  enc->output_len = 2;
  A_CALLOC(enc->output, 2);

  enc->prepared = false;

  return enc;
}

void h264_encoder_destroy(h264_encoder_s *enc) {
  _h264_encoder_cleanup(enc);

  free(enc->output);
  free(enc);
}

void _h264_encoder_cleanup(h264_encoder_s *enc) {
  if (!enc->prepared)
    return;

  int type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
  int ret = ioctl(enc->fd, VIDIOC_STREAMOFF, &type);
  fprintf(stderr, "Output stream: %d %u\n", ret, errno);

  type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
  ret = ioctl(enc->fd, VIDIOC_STREAMOFF, &type);
  fprintf(stderr, "Capture stream: %d %u\n", ret, errno);

  for (int i = 0; i < enc->output_len; ++i) {
    munmap(enc->output[i].start, enc->output[i].length);
  }
  munmap(enc->capture.start, enc->capture.length);

  close(enc->fd);

  enc->fd = -1;
  enc->prepared = false;
}

bool h264_encoder_is_prepared_for(h264_encoder_s *enc, const frame_s *frame,
                                  bool zero_copy) {
  return enc->prepared && enc->width == frame->width && enc->height == frame->height;
}

int h264_encoder_prepare(h264_encoder_s *enc, const frame_s *frame,
                         bool zero_copy) {
  _h264_encoder_cleanup(enc);

  enc->fd = open("/dev/video11", O_RDWR);
  assert(enc->fd > 0);

  struct v4l2_capability input;
  int ret = ioctl(enc->fd, VIDIOC_QUERYCAP, &input);
  fprintf(stderr, "Caps       : %d %d %X %s\n", ret, errno, input.device_caps,
          input.driver);

  struct v4l2_format fm;
  fm.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
  ret = ioctl(enc->fd, VIDIOC_G_FMT, &fm);

  struct v4l2_pix_format_mplane *mp = &fm.fmt.pix_mp;
  fprintf(stderr, "Output fmt : %d %d (res: %ux%u) (pl: %u) %u %u %u %u\n", ret,
          errno, mp->height, mp->width, mp->num_planes, mp->colorspace,
          mp->pixelformat, mp->plane_fmt[0].bytesperline,
          mp->plane_fmt[0].sizeimage);

  mp->width = frame->width;
  mp->height = frame->height;
  mp->pixelformat = V4L2_PIX_FMT_RGB24;

  LOG_DEBUG("Res: %ux%u Pix: %d", mp->width, mp->height, mp->pixelformat);

  printf("Pixel format: %u\n", mp->pixelformat);

  ret = ioctl(enc->fd, VIDIOC_S_FMT, &fm);
  fprintf(stderr, "Output set: %d %d (res: %ux%u) %u %u %u\n", ret, errno,
          mp->height, mp->width, mp->plane_fmt[0].bytesperline,
          mp->plane_fmt[0].sizeimage, mp->pixelformat);

  fm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
  ret = ioctl(enc->fd, VIDIOC_G_FMT, &fm);
  fprintf(stderr, "Capture fmt : %d %d (res: %ux%u) (pl: %u) %u %u %u %u\n",
          ret, errno, mp->height, mp->width, mp->num_planes, mp->colorspace,
          mp->pixelformat, mp->plane_fmt[0].bytesperline,
          mp->plane_fmt[0].sizeimage);

  struct v4l2_streamparm stream;
  memset(&stream, 0, sizeof(stream));
  stream.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
  stream.parm.output.timeperframe.numerator = 1;
  stream.parm.output.timeperframe.denominator = 30;

  ret = ioctl(enc->fd, VIDIOC_S_PARM, &stream);
  fprintf(stderr, "Output param set: %u %u %u %u\n", ret, errno,
          stream.parm.output.timeperframe.numerator,
          stream.parm.output.timeperframe.denominator);

  struct v4l2_requestbuffers buf;
  buf.memory = V4L2_MEMORY_MMAP;

  buf.count = 2;
  buf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
  ret = ioctl(enc->fd, VIDIOC_REQBUFS, &buf);
  fprintf(stderr, "Req buffers: %d %u %u\n", ret, errno, buf.count);

  map(enc->fd, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE, enc->output, enc->output_len);
  fprintf(stderr, "Output buffers: (len: %zu)\n", enc->output[0].length);

  buf.count = 1;
  buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
  ret = ioctl(enc->fd, VIDIOC_REQBUFS, &buf);
  fprintf(stderr, "Req buffers: %d %u %u\n", ret, errno, buf.count);

  map(enc->fd, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE, &enc->capture, 1);
  fprintf(stderr, "Capture buffers: (len: %zu)\n", enc->capture.length);

  ret = ioctl(enc->fd, VIDIOC_QBUF, &enc->capture.inner);
  fprintf(stderr, "Queue capture: %d %u\n", ret, errno);

  int type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
  ret = ioctl(enc->fd, VIDIOC_STREAMON, &type);
  fprintf(stderr, "Output stream: %d %u\n", ret, errno);

  type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
  ret = ioctl(enc->fd, VIDIOC_STREAMON, &type);
  fprintf(stderr, "Capture stream: %d %u\n", ret, errno);

  int cnt = min_u(frame->used, enc->output[0].length);
  memcpy(enc->output[0].start, frame->data, cnt);
  enc->output[0].planes.bytesused = cnt;

  enc->output[0].inner.flags =
      V4L2_BUF_FLAG_TIMESTAMP_COPY | V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;

  enc->start_ts = get_now_monotonic();
  enc->output[0].inner.timestamp.tv_sec = 0;
  enc->output[0].inner.timestamp.tv_usec = 0;

  struct v4l2_control ctrl;
  ctrl.id = V4L2_CID_MPEG_VIDEO_REPEAT_SEQ_HEADER;
  ctrl.value = 1;
  ret = ioctl(enc->fd, VIDIOC_S_CTRL, &ctrl);
  if (ret < 0) {
    LOG_ERROR("Can't force inline header %d", ret);
    return -1;
  }

  ctrl.id = V4L2_CID_MPEG_VIDEO_H264_I_PERIOD;
  ctrl.value = 60;
  ret = ioctl(enc->fd, VIDIOC_S_CTRL, &ctrl);
  if (ret < 0) {
    LOG_ERROR("Can't set iframe period %d", ret);
    return -1;
  }

  ret = ioctl(enc->fd, VIDIOC_QBUF, &enc->output[0].inner);
  if (ret < 0) {
    fprintf(stderr, "Queue output: %d %u\n", ret, errno);
    return -1;
  }
  enc->last_online = -1;
  enc->frame = 0;
  enc->prepared = true;
  enc->width = frame->width;
  enc->height = frame->height;
  return 0;
}

int h264_encoder_compress(h264_encoder_s *enc, const frame_s *src,
                          int src_vcsm_handle, frame_s *dest, bool force_key) {
  assert(src->used > 0);

  frame_copy_meta(src, dest);
  dest->encode_begin_ts = get_now_monotonic();
  dest->format = V4L2_PIX_FMT_H264;
  dest->stride = 0;

  force_key = (force_key || enc->last_online != src->online);

  if (_h264_encoder_compress_raw(enc, src, src_vcsm_handle, dest, force_key) < 0) {
    LOG_ERROR("H264: Encoder disabled due error (compress)");
    return -1;
  }

  dest->encode_end_ts = get_now_monotonic();
  LOG_VERBOSE("H264: Compressed new frame: size=%zu, time=%0.3Lf, force_key=%d",
              dest->used, dest->encode_end_ts - dest->encode_begin_ts,
              force_key);

  enc->last_online = src->online;
  return 0;
}

static int _h264_encoder_compress_raw(h264_encoder_s *enc, const frame_s *src,
                                      int src_vcsm_handle, frame_s *dest,
                                      bool force_key) {
  struct v4l2_buffer dq_out;
  dq_out.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
  dq_out.memory = V4L2_MEMORY_MMAP;
  dq_out.length = 1;

  struct v4l2_plane out_planes;
  memset(&out_planes, 0, sizeof(out_planes));
  dq_out.m.planes = &out_planes;

  if (force_key) {
    struct v4l2_control ctrl;
    ctrl.id = V4L2_CID_MPEG_VIDEO_FORCE_KEY_FRAME;
    ctrl.value = 1;
    int ret = ioctl(enc->fd, VIDIOC_S_CTRL, &ctrl);
    if (ret < 0) {
      LOG_ERROR("Can't force keyframe %d", ret);
		  return -1;
    }
  }

  int ret = ioctl(enc->fd, VIDIOC_DQBUF, &dq_out);
  if (ret < 0) {
    fprintf(stderr, "Dequeue output: %d %u\n", ret, errno);
    return -1;
  } else {
    buffer *avail = &enc->output[dq_out.index];

    uint32_t cnt = min_u(src->used, avail->length);
    memcpy(avail->start, src->data, cnt);
    avail->planes.bytesused = cnt;

    avail->inner.flags =
        V4L2_BUF_FLAG_TIMESTAMP_COPY | V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;

    double now = get_now_monotonic() - enc->start_ts;
    avail->inner.timestamp.tv_sec = floor(now);
    avail->inner.timestamp.tv_usec = 1000 * 1000 * (now - floor(now));

    ret = ioctl(enc->fd, VIDIOC_QBUF, &avail->inner);
    if (ret < 0) {
      fprintf(stderr, "Queue output: %d %u\n", ret, errno);
      return -1;
    }
  }

  struct v4l2_buffer dq_capt;
  dq_capt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
  dq_capt.memory = V4L2_MEMORY_MMAP;
  dq_capt.length = 1;

  struct v4l2_plane capt_planes;
  memset(&capt_planes, 0, sizeof(capt_planes));
  dq_capt.m.planes = &capt_planes;

  ret = ioctl(enc->fd, VIDIOC_DQBUF, &dq_capt);
  if (ret < 0 && errno != EAGAIN) {
    fprintf(stderr, "Dequeue capture: %d %u\n", ret, errno);
    return -1;
  } else if (ret == 0) {
    frame_set_data(dest, enc->capture.start, dq_capt.m.planes[0].bytesused);
    ret = ioctl(enc->fd, VIDIOC_QBUF, &enc->capture.inner);
    if (ret < 0) {
      fprintf(stderr, "Queue capture: %d %u\n", ret, errno);
      return -1;
    }
  }
  return 0;
}