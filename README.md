# µStreamer
[![CI](https://github.com/pikvm/ustreamer/workflows/CI/badge.svg)](https://github.com/pikvm/ustreamer/actions?query=workflow%3ACI)
[![Discord](https://img.shields.io/discord/580094191938437144?logo=discord)](https://discord.gg/bpmXfz5)

µStreamer is a lightweight and very quick server to stream [MJPEG](https://en.wikipedia.org/wiki/Motion_JPEG) video from any V4L2 device to the net. All new browsers have native support of this video format, as well as most video players such as mplayer, VLC etc.
µStreamer is a part of the [PiKVM](https://github.com/pikvm/pikvm) project designed to stream [VGA](https://www.amazon.com/dp/B0126O0RDC) and [HDMI](https://auvidea.com/b101-hdmi-to-csi-2-bridge-15-pin-fpc/) screencast hardware data with the highest resolution and FPS possible.

µStreamer is very similar to [mjpg-streamer](https://github.com/jacksonliam/mjpg-streamer) with ```input_uvc.so``` and ```output_http.so``` plugins, however, there are some major differences. The key ones are:

| **Feature** | **µStreamer** | **mjpg-streamer** |
|----------|---------------|-------------------|
| Multithreaded JPEG encoding | ✔ | ✘ |
| Hardware image encoding<br>on Raspberry Pi | ✔ | ✘ |
| Behavior when the device<br>is disconnected while streaming | ✔ Shows a black screen<br>with ```NO SIGNAL``` on it<br>until reconnected | ✘ Stops the streaming <sup>1</sup> |
| [DV-timings](https://linuxtv.org/downloads/v4l-dvb-apis-new/userspace-api/v4l/dv-timings.html) support -<br>the ability to change resolution<br>on the fly by source signal | ✔ | ☹ Partially yes <sup>1</sup> |
| Option to skip frames when streaming<br>static images by HTTP to save the traffic | ✔ <sup>2</sup> | ✘ |
| Streaming via UNIX domain socket | ✔ | ✘ |
| Systemd socket activation | ✔ | ✘ |
| Debug logs without recompiling,<br>performance statistics log,<br>access to HTTP streaming parameters | ✔ | ✘ |
| Option to serve files<br>with a built-in HTTP server | ✔ | ☹ Regular files only |
| Signaling about the stream state<br>on GPIO using [libgpiod](https://git.kernel.org/pub/scm/libs/libgpiod/libgpiod.git/about) | ✔ | ✘ |
| Access to webcam controls (focus, servos)<br>and settings such as brightness via HTTP | ✘ | ✔ |
| Compatibility with mjpg-streamer's API | ✔ | :) |

Footnotes:
  * ```1``` Long before µStreamer, I made a [patch](https://github.com/jacksonliam/mjpg-streamer/pull/164) to add DV-timings support to mjpg-streamer and to keep it from hanging up no device disconnection. Alas, the patch is far from perfect and I can't guarantee it will work every time - mjpg-streamer's source code is very complicated and its structure is hard to understand. With this in mind, along with needing multithreading and JPEG hardware acceleration in the future, I decided to make my own stream server from scratch instead of supporting legacy code.

  * ```2``` This feature allows to cut down outgoing traffic several-fold when streaming HDMI, but it increases CPU usage a little bit. The idea is that HDMI is a fully digital interface and each captured frame can be identical to the previous one byte-wise. There's no need to stream the same image over the net several times a second. With the `--drop-same-frames=20` option enabled, µStreamer will drop all the matching frames (with a limit of 20 in a row). Each new frame is matched with the previous one first by length, then using ```memcmp()```.

-----
# TL;DR
If you're going to live-stream from your backyard webcam and need to control it, use mjpg-streamer. If you need a high-quality image with high FPS - µStreamer for the win.

-----
# Installation

## Building
You need to download the µStreamer onto your system and build it from the sources.

* AUR has a package for Arch Linux: https://aur.archlinux.org/packages/ustreamer.
* Fedora: https://src.fedoraproject.org/rpms/ustreamer.
* Ubuntu: https://packages.ubuntu.com/jammy/ustreamer.
* Debian: https://packages.debian.org/sid/ustreamer
* FreeBSD port: https://www.freshports.org/multimedia/ustreamer.

### Preconditions
You'll need  ```make```, ```gcc```, ```libevent``` with ```pthreads``` support, ```libjpeg9```/```libjpeg-turbo``` and ```libbsd``` (only for Linux).

* Arch: `sudo pacman -S libevent libjpeg-turbo libutil-linux libbsd`.
* Raspbian: `sudo apt install libevent-dev libjpeg9-dev libbsd-dev`. Add `libgpiod-dev` for `WITH_GPIO=1` and `libsystemd-dev` for `WITH_SYSTEMD=1` and `libasound2-dev libspeex-dev libspeexdsp-dev libopus-dev` for `WITH_JANUS=1`.
* Debian/Ubuntu: `sudo apt install build-essential libevent-dev libjpeg-dev libbsd-dev`.
* Alpine: `sudo apk add libevent-dev libbsd-dev libjpeg-turbo-dev musl-dev`. Build with `WITH_PTHREAD_NP=0`.

To enable GPIO support install [libgpiod](https://git.kernel.org/pub/scm/libs/libgpiod/libgpiod.git/about) and pass option ```WITH_GPIO=1```. If the compiler reports about a missing function ```pthread_get_name_np()``` (or similar), add option ```WITH_PTHREAD_NP=0``` (it's enabled by default). For the similar error with ```setproctitle()``` add option ```WITH_SETPROCTITLE=0```.

> **Note**
> Raspian: In case your version of Raspian is too old for there to be a libjpeg9 package, use `libjpeg8-dev` instead: `E: Package 'libjpeg9-dev' has no installation candidate`.

### Make
The most convenient process is to clone the µStreamer Git repository onto your system. If you don't have Git installed and don't want to install it either, you can download and unzip the sources from GitHub using `wget https://github.com/pikvm/ustreamer/archive/refs/heads/master.zip`.

```
$ git clone --depth=1 https://github.com/pikvm/ustreamer
$ cd ustreamer
$ make
$ ./ustreamer --help
```

## Update
Assuming you have a µStreamer clone as discussed above you can update µStreamer as follows.

```
$ cd ustreamer
$ git pull
$ make clean
$ make
```

-----
# Usage
**For M2M hardware encoding on Raspberry Pi, you need at least 5.15.32 kernel. OpenMAX and MMAL support on older kernels is deprecated and removed.**

Without arguments, ```ustreamer``` will try to open ```/dev/video0``` with 640x480 resolution and start streaming on  ```http://127.0.0.1:8080```. You can override this behavior using parameters ```--device```, ```--host``` and ```--port```. For example, to stream to the world, run:
```
# ./ustreamer --device=/dev/video1 --host=0.0.0.0 --port=80
```

:exclamation: Please note that since µStreamer v2.0 cross-domain requests were disabled by default for [security reasons](https://developer.mozilla.org/en-US/docs/Web/HTTP/CORS). To enable the old behavior, use the option `--allow-origin=\*`.

The recommended way of running µStreamer with [Auvidea B101](https://www.raspberrypi.org/forums/viewtopic.php?f=38&t=120702&start=400#p1339178) on Raspberry Pi:
```
$ ./ustreamer \
    --format=uyvy \ # Device input format
    --encoder=m2m-image \ # Hardware encoding on V4L2 M2M driver
    --workers=3 \ # Workers number
    --persistent \ # Don't re-initialize device on timeout (for example when HDMI cable was disconnected)
    --dv-timings \ # Use DV-timings
    --drop-same-frames=30 # Save the traffic
```

:exclamation: Please note that to use `--drop-same-frames` for different browsers you need to use some specific URL `/stream` parameters (see URL `/` for details).

You can always view the full list of options with ```ustreamer --help```.

-----
# Docker (Raspberry Pi 4 HDMI)

## Preparations
Add following lines to /boot/firmware/usercfg.txt:

```
gpu_mem=128
dtoverlay=tc358743
```

Check size of CMA:

```
$ dmesg | grep cma-reserved
[    0.000000] Memory: 7700524K/8244224K available (11772K kernel code, 1278K rwdata, 4320K rodata, 4096K init, 1077K bss, 281556K reserved, 262144K cma-reserved)
```

If it is smaller than 128M add following to /boot/firmware/cmdline.txt:

```
cma=128M
```

Save changes and reboot.

## Launch
Start container:

```
$ docker run --device /dev/video0:/dev/video0 -e EDID=1 -p 8080:8080 pikvm/ustreamer:latest
```

Then access the web interface at port 8080 (e.g. http://raspberrypi.local:8080).

## Custom config
```
$ docker run --rm pikvm/ustreamer:latest \
    --format=uyvy \
    --workers=3 \
    --persistent \
    --dv-timings \
    --drop-same-frames=30
```

## EDID
Add `-e EDID=1` to set HDMI EDID before starting ustreamer. Use together with `-e EDID_HEX=xx` to specify custom EDID data.

-----
# Raspberry Pi Camera Example

Example usage for the Raspberry Pi v3 camera (required `libcamerify` which is located in `libcamera-tools` on Raspbian):
```
$ sudo modprobe bcm2835-v4l2
$ libcamerify ./ustreamer --host :: -e m2m-image
```


Example usage for the Raspberry Pi v1 camera:
```
$ sudo modprobe bcm2835-v4l2
$ ./ustreamer --host :: -m jpeg --device-timeout=5 --buffers=3 -r 2592x1944
```

:exclamation: Please note that newer camera models have a different maximum resolution. You can see the supported resolutions at the [PiCamera documentation](https://picamera.readthedocs.io/en/release-1.13/fov.html#sensor-modes).

:exclamation: If you get a poor framerate, it could be that the camera is switched to photo mode, which produces a low framerate (but a higher quality picture). This is because `bcm2835-v4l2` switches to photo mode at resolutions higher than `1280x720`. To work around this, pass the `max_video_width` and `max_video_height` module parameters like so:

```
$ modprobe bcm2835-v4l2 max_video_width=2592 max_video_height=1944
```

-----
# Integrations

## Janus
µStreamer supports bandwidth-efficient streaming using [H.264 compression](https://en.wikipedia.org/wiki/Advanced_Video_Coding) and the Janus WebRTC server. See the [Janus integration guide](docs/h264.md) for full details.

## Nginx
When uStreamer is behind an Nginx proxy, it's buffering behavior introduces latency into the video stream. It's possible to disable Nginx's buffering to eliminate the additional latency:

```nginx
location /stream {
    postpone_output 0;
    proxy_buffering off;
    proxy_ignore_headers X-Accel-Buffering;
    proxy_pass http://ustreamer;
}
```

-----
# Tips & tricks for v4l2
v4l2 utilities provide the tools to manage USB webcam setting and information. Scripts can be use to make adjustments and run manually or with cron. Running in cron for example to change the exposure settings at certain times of day. The package is available in all Linux distributions and is usually called `v4l-utils`.

* List of available video devices: `v4l2-ctl --list-devices`.
* List available control settings: `v4l2-ctl -d /dev/video0 --list-ctrls`.
* List available video formats: `v4l2-ctl -d /dev/video0 --list-formats-ext`.
* Read the current setting: `v4l2-ctl -d /dev/video0 --get-ctrl=exposure_auto`.
* Change the setting value: `v4l2-ctl -d /dev/video0 --set-ctrl=exposure_auto=1`.

[Here](https://www.kurokesu.com/main/2016/01/16/manual-usb-camera-settings-in-linux/) you can find more examples. Documentation is available in [`man v4l2-ctl`](https://www.mankier.com/1/v4l2-ctl).

-----
# See also
* [Running uStreamer via systemd service](https://github.com/pikvm/ustreamer/issues/16).

-----
# License
Copyright (C) 2018-2023 by Maxim Devaev mdevaev@gmail.com

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see https://www.gnu.org/licenses/.
