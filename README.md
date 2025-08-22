### µStreamer

[µStreamer](https://github.com/pikvm/ustreamer) 是一个轻量快速的 MJPEG 视频流服务，可将 V4L2 设备上的 MJPEG 视频流传输到网络。主流浏览器以及 mplayer、VLC 等大多数视频播放器都支持这种视频格式。它是 [PiKVM](https://github.com/pikvm/pikvm) 项目的一部分，旨在以尽可能高的分辨率和帧率通过网络传输 VGA 或 HDMI 的视频数据。

此分支作者建立了基于 [PiKVM](https://github.com/pikvm/pikvm) 项目的 [One-KVM](https://github.com/mofeng-git/One-KVM)，该项目旨在将 PiKVM 的功能扩展到其他平台，如 X86、ARM。故作者对 [µStreamer](https://github.com/pikvm/ustreamer) 进行了**分支**和**修改**：添加**FFmpeg 硬件编码器**，以支持树莓派以外平台的 H.264 视频编码，并提供硬件加速编码支持。

### 编译

基于 Ubuntu/Debian 发行版，直接编译所需依赖：
```bash
apt install  build-essential libssl-dev libffi-dev libevent-dev libjpeg-dev libbsd-dev libudev-dev git pkg-config
```
启用 `WITH_PYTHON=1` 选项所需额外依赖：
```bash
apt install python3-dev python3-build
```
启用 `WITH_JANUS=1` 选项所需额外依赖：
```bash
apt install janus-dev libasound2-dev  libspeex-dev libspeexdsp-dev libopus-dev
sed --in-place --expression 's|^#include "refcount.h"$|#include "../refcount.h"|g' /usr/include/janus/plugins/plugin.h
```
启用 `WITH_FFMPEG=1` 选项所需额外依赖（支持硬件编码）：
```bash
apt install ffmpeg libavcodec-dev libavformat-dev libavutil-dev libswscale-dev
```


```bash
git clone --depth=1 https://github.com/mofeng-git/ustreamer
cd ustreamer
make
#make WITH_PYTHON=1 WITH_JANUS=1 WITH_FFMPEG=1
./ustreamer --help
```


-----
### 使用

基本使用示例：采集 `/dev/video0` 设备的视频流，并在 `0.0.0.0:8080` 提供网页服务：
```bash
./ustreamer --device=/dev/video0 --host=0.0.0.0 --port=8080
```

使用 libx264 编码 H.264 视频示例：
```bash
./ustreamer \
    --format=mjpeg \ # 视频输入格式
    --encoder=LIBX264-VIDEO \ # 使用 libx264 编码器
    --persistent \ # 屏蔽信号源重复出错（例如 HDMI 线缆断开时）
    --h264-sink=test.h264 # 指定H.264 视频流共享内存

./ustreamer-dump \
    --sink test.h264 \ # 指定共享内存位置
    --output test.h264 # 输入共享内存内容到文件
```

使用 FFmpeg 硬件编码 H.264 视频示例：
```bash
# AMD VAAPI 硬件编码
./ustreamer --device=/dev/video0 --encoder=FFMPEG-VIDEO \
  --h264-sink=output.h264 --h264-hwenc=vaapi \
  --h264-bitrate=3000 --host=0.0.0.0 --port=8080

# NVIDIA NVENC 硬件编码
./ustreamer --device=/dev/video0 --encoder=FFMPEG-VIDEO \
  --h264-sink=output.h264 --h264-hwenc=nvenc \
  --h264-bitrate=3000 --host=0.0.0.0 --port=8080

# 硬件编码失败时自动回退到软件编码
./ustreamer --device=/dev/video0 --encoder=FFMPEG-VIDEO \
  --h264-sink=output.h264 --h264-hwenc=vaapi \
  --h264-hwenc-fallback --h264-bitrate=3000
```

使用 ```ustreamer --help``` 可以查看完整的选项列表。

### 硬件编码支持

µStreamer 现在尝试开始实验性支持 FFmpeg 硬件编码，可显著提高编码性能并降低 CPU 使用率。未来可能支持的硬件编码器包括：

- **AMD GPU (VAAPI)**: `--h264-hwenc=vaapi`
- **NVIDIA GPU (NVENC)**: `--h264-hwenc=nvenc`
- **Intel GPU (Quick Sync)**: `--h264-hwenc=vaapi`
- **V4L2 M2M (ARM 设备)**: `--h264-hwenc=v4l2m2m`

-----
### 版权声明
Copyright (C) 2018-2025 by Maxim Devaev mdevaev@gmail.com

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
