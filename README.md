# µStreamer
[![CI](https://github.com/pikvm/ustreamer/workflows/CI/badge.svg)](https://github.com/pikvm/ustreamer/actions?query=workflow%3ACI)
[![Discord](https://img.shields.io/discord/580094191938437144?logo=discord)](https://discord.gg/bpmXfz5)

[[Русская версия]](README.ru.md)

µStreamer is a lightweight and very quick server to stream [MJPG](https://en.wikipedia.org/wiki/Motion_JPEG) video from any V4L2 device to the net. All new browsers have native support of this video format, as well as most video players such as mplayer, VLC etc.
µStreamer is a part of the [Pi-KVM](https://github.com/pikvm/pikvm) project designed to stream [VGA](https://www.amazon.com/dp/B0126O0RDC) and [HDMI](https://auvidea.com/b101-hdmi-to-csi-2-bridge-15-pin-fpc/) screencast hardware data with the highest resolution and FPS possible.

µStreamer is very similar to [mjpg-streamer](https://github.com/jacksonliam/mjpg-streamer) with ```input_uvc.so``` and ```output_http.so``` plugins, however, there are some major differences. The key ones are:

| **Feature** | **µStreamer** | **mjpg-streamer** |
|----------|---------------|-------------------|
| Multithreaded JPEG encoding | ✔ | ✘ |
| [OpenMAX IL](https://www.khronos.org/openmaxil) hardware acceleration<br>on Raspberry Pi | ✔ | ✘ |
| Behavior when the device<br>is disconnected while streaming | ✔ Shows a black screen<br>with ```NO SIGNAL``` on it<br>until reconnected | ✘ Stops the streaming <sup>1</sup> |
| [DV-timings](https://linuxtv.org/downloads/v4l-dvb-apis-new/userspace-api/v4l/dv-timings.html) support -<br>the ability to change resolution<br>on the fly by source signal | ✔ | ☹ Partially yes <sup>1</sup> |
| Option to skip frames when streaming<br>static images by HTTP to save the traffic | ✔ <sup>2</sup> | ✘ |
| Streaming via UNIX domain socket | ✔ | ✘ |
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
# Building
You'll need  ```make```, ```gcc```, ```libevent``` with ```pthreads``` support, ```libjpeg8```/```libjpeg-turbo``` and ```libbsd``` (only for Linux).

* Arch: `sudo pacman -S libevent libjpeg-turbo libutil-linux libbsd`.
* Raspbian: `sudo apt install libevent-dev libjpeg8-dev libbsd-dev`. Add `libraspberrypi-dev` for `WITH_OMX=1` and `libgpiod` for `WITH_GPIO=1`.
* Debian/Ubuntu: `sudo apt install build-essential libevent-dev libjpeg-dev libbsd-dev`.

On Raspberry Pi you can build the program with OpenMAX IL. To do this pass option ```WITH_OMX=1``` to ```make```. To enable GPIO support install [libgpiod](https://git.kernel.org/pub/scm/libs/libgpiod/libgpiod.git/about) and pass option ```WITH_GPIO=1```. If the compiler reports about a missing function ```pthread_get_name_np()``` (or similar), add option ```WITH_PTHREAD_NP=0``` (it's enabled by default). For the similar error with ```setproctitle()``` add option ```WITH_SETPROCTITLE=0```.

```
$ git clone --depth=1 https://github.com/pikvm/ustreamer
$ cd ustreamer
$ make
$ ./ustreamer --help
```

AUR has a package for Arch Linux: https://aur.archlinux.org/packages/ustreamer. It should compile automatically with OpenMAX IL on Raspberry Pi, if the corresponding headers are present in ```/opt/vc/include```.
FreeBSD port: https://www.freshports.org/multimedia/ustreamer.

-----
# Usage
Without arguments, ```ustreamer``` will try to open ```/dev/video0``` with 640x480 resolution and start streaming on  ```http://127.0.0.1:8080```. You can override this behavior using parameters ```--device```, ```--host``` and ```--port```. For example, to stream to the world, run:
```
# ./ustreamer --device=/dev/video1 --host=0.0.0.0 --port=80
```

:exclamation: Please note that since µStreamer v2.0 cross-domain requests were disabled by default for [security reasons](https://developer.mozilla.org/en-US/docs/Web/HTTP/CORS). To enable the old behavior, use the option `--allow-origin=\*`.

The recommended way of running µStreamer with [Auvidea B101](https://www.raspberrypi.org/forums/viewtopic.php?f=38&t=120702&start=400#p1339178) on Raspberry Pi:
```bash
$ ./ustreamer \
    --format=uyvy \ # Device input format
    --encoder=omx \ # Hardware encoding with OpenMAX
    --workers=3 \ # Maximum workers for OpenMAX
    --persistent \ # Don't re-initialize device on timeout (for example when HDMI cable was disconnected)
    --dv-timings \ # Use DV-timings
    --drop-same-frames=30 # Save the traffic
```

:exclamation: Please note that to use `--drop-same-frames` for different browsers you need to use some specific URL `/stream` parameters (see URL `/` for details).

You can always view the full list of options with ```ustreamer --help```.

-----
# Raspberry Pi Camera Example
Example usage for the Raspberry Pi v1 camera:
```bash
$ sudo modprobe bcm2835-v4l2
$ ./ustreamer --host :: -m jpeg --device-timeout=5 --buffers=3 -r 2592x1944
```

:exclamation: Please note that newer camera models have a different maximum resolution. You can see the supported resolutions at the [PiCamera documentation](https://picamera.readthedocs.io/en/release-1.13/fov.html#sensor-modes).

:exclamation: If you get a poor framerate, it could be that the camera is switched to photo mode, which produces a low framerate (but a higher quality picture). This is because `bcm2835-v4l2` switches to photo mode at resolutions higher than `1280x720`. To work around this, pass the `max_video_width` and `max_video_height` module parameters like so:

```bash
$ modprobe bcm2835-v4l2 max_video_width=2592 max_video_height=1944
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
* [uStreamer Ansible Role](https://github.com/mtlynch/ansible-role-ustreamer): Use [Ansible](https://docs.ansible.com/ansible/latest/index.html) to compile uStreamer and install it as a systemd service automatically.

-----
# License
Copyright (C) 2018-2021 by Maxim Devaev mdevaev@gmail.com

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
