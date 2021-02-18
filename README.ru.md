# µStreamer
[![CI](https://github.com/pikvm/ustreamer/workflows/CI/badge.svg)](https://github.com/pikvm/ustreamer/actions?query=workflow%3ACI)
[![Discord](https://img.shields.io/discord/580094191938437144?logo=discord)](https://discord.gg/bpmXfz5)

[[English version]](README.md)


µStreamer - это маленький и очень быстрый сервер, который позволяет организовать трансляцию видео в формате [MJPG](https://en.wikipedia.org/wiki/Motion_JPEG) с любого устройства V4L2 в сеть. Этот формат нативно поддерживается всеми современными браузерами и большинством приложений для просмотра видео (mplayer, VLC и так далее). µStreamer был разработан в рамках проекта [Pi-KVM](https://github.com/pikvm/pikvm) специально для стриминга с устройств видеозахвата [VGA](https://www.amazon.com/dp/B0126O0RDC) и [HDMI](https://auvidea.com/b101-hdmi-to-csi-2-bridge-15-pin-fpc/) с максимально возможным разрешением и FPS, которые только позволяет железо.

Функционально µStreamer очень похож на [mjpg-streamer](https://github.com/jacksonliam/mjpg-streamer) при использовании им плагинов ```input_uvc.so``` и ```output_http.so```, однако имеет ряд серьезных отличий. Основные приведены в этой таблице:

| **Фича** | **µStreamer** | **mjpg-streamer** |
|----------|---------------|-------------------|
| Многопоточное кодирование JPEG | ![#00aa00](https://placehold.it/15/00aa00/000000?text=+) Есть | ![#f03c15](https://placehold.it/15/f03c15/000000?text=+) Нет |
| Аппаратное кодирование с помощью [OpenMAX IL](https://www.khronos.org/openmaxil) на Raspberry Pi | ![#00aa00](https://placehold.it/15/00aa00/000000?text=+) Есть | ![#f03c15](https://placehold.it/15/f03c15/000000?text=+) Нет |
| Поведение при физическом отключении<br>устройства от сервера во время работы | ![#00aa00](https://placehold.it/15/00aa00/000000?text=+) Транслирует черный экран<br>с надписью ```NO SIGNAL```,<br>пока устройство не будет подключено снова | ![#f03c15](https://placehold.it/15/f03c15/000000?text=+) Прерывает трансляцию <sup>1</sup> |
| Поддержка [DV-таймингов](https://linuxtv.org/downloads/v4l-dvb-apis-new/userspace-api/v4l/dv-timings.html) - возможности<br>изменения  параметров разрешения<br>трансляции на лету по сигналу<br>источника (устройства видеозахвата) | ![#00aa00](https://placehold.it/15/00aa00/000000?text=+) Есть | ![#ffaa00](https://placehold.it/15/ffaa00/000000?text=+) Условно есть <sup>1</sup> |
| Возможность пропуска фреймов при передаче<br>статического изображения по HTTP<br>для экономии трафика | ![#00aa00](https://placehold.it/15/00aa00/000000?text=+) Есть <sup>2</sup> | ![#f03c15](https://placehold.it/15/f03c15/000000?text=+) Нет |
| Стрим через UNIX domain socket | ![#00aa00](https://placehold.it/15/00aa00/000000?text=+) Есть | ![#f03c15](https://placehold.it/15/f03c15/000000?text=+) Нет |
| Дебаг-логи без перекомпиляции,<br>логгирование статистики производительности,<br>возможность получения параметров<br>трансляции по HTTP | ![#00aa00](https://placehold.it/15/00aa00/000000?text=+) Есть | ![#f03c15](https://placehold.it/15/f03c15/000000?text=+) Нет |
| Возможность сервить файлы встроенным<br>HTTP-сервером  | ![#00aa00](https://placehold.it/15/00aa00/000000?text=+) Есть | ![#ffaa00](https://placehold.it/15/ffaa00/000000?text=+) Нет каталогов |
| Вывод сигналов о состоянии стрима на GPIO<br>с помощью [libgpiod](https://git.kernel.org/pub/scm/libs/libgpiod/libgpiod.git/about) | ![#00aa00](https://placehold.it/15/00aa00/000000?text=+) Есть | ![#f03c15](https://placehold.it/15/f03c15/000000?text=+) Нет |
| Поддержка контролов веб-камер (фокус,<br> движение сервами) и всяких настроек,<br> типа яркости, через HTTP | ![#f03c15](https://placehold.it/15/f03c15/000000?text=+) Нет | ![#00aa00](https://placehold.it/15/00aa00/000000?text=+) Есть |
| Совместимость с API mjpg-streamer'а | ![#00aa00](https://placehold.it/15/00aa00/000000?text=+) Есть | :) |

Сносочки:
  * ```1``` Еще до написания µStreamer, я запилил [патч](https://github.com/jacksonliam/mjpg-streamer/pull/164), добавляющий в mjpg-streamer поддержку DV-таймингов и предотвращающий его зависание при отключении устройства. Однако патч, увы, далек от совершенства и я не гарантирую его стопроцентную работоспособность, поскольку код mjpg-streamer чрезвычайно запутан и очень плохо структурирован. Учитывая это, а также то, что в дальнейшем мне потребовались многопоточность и аппаратное кодирование JPEG, было принято решение написать свой стрим-сервер с нуля, чтобы не тратить силы на поддержку лишнего легаси.

  * ```2``` Это фича позволяет в несколько раз снизить объем исходящего трафика при трансляции HDMI, однако немного увеличивает загрузку процессора. Суть в том, что HDMI - полностью цифровой интерфейс, и новый захваченный фрейм может быть идентичен предыдущему в точности до байта. В этом случае нет нужды передавать одну и ту же картинку по сети несколько раз в секунду. При использовании опции `--drop-same-frames=20`, µStreamer будет дропать все одинаковые фреймы, но не более 20 подряд. Новый фрейм сравнивается с предыдущим сначала по длине, а затем помощью ```memcmp()```.

-----
# TL;DR
Если вам нужно вещать стрим с уличной камеры и управлять ее параметрами - возьмите mjpg-streamer. Если же вам нужно очень качественное изображение с высоким FPS - µStreamer ваш бро.

-----
# Сборка
Для сборки вам понадобятся ```make```, ```gcc```, ```libevent``` с поддержкой ```pthreads```, ```libjpeg8```/```libjpeg-turbo```, ```libuuid``` и ```libbsd``` (только для Linux).

* Arch: `sudo pacman -S libevent libjpeg-turbo libutil-linux libbsd`.
* Raspbian: `sudo apt install libevent-dev libjpeg8-dev uuid-dev libbsd-dev`.  Добавьте `libraspberrypi-dev` для сборки с `WITH_OMX=1` и `libgpiod` для `WITH_GPIO=1`.
* Debian: `sudo apt install build-essential libevent-dev libjpeg62-turbo-dev uuid-dev libbsd-dev`.
* Ubuntu 20.04 x86_64: `sudo apt install build-essential libevent-dev libjpeg-dev libjpeg62-dev uuid-dev libbsd-dev make gcc libjpeg8 libjpeg-turbo8 libuuid1 libbsd0`.

На Raspberry Pi программу можно собрать с поддержкой OpenMAX IL. Для этого передайте ```make``` параметр ```WITH_OMX=1```. Для включения сборки с поддержкой GPIO установите [libgpiod](https://git.kernel.org/pub/scm/libs/libgpiod/libgpiod.git/about) и добавьте параметр ```WITH_GPIO=1```. Если при сборке компилятор ругается на отсутствие функции ```pthread_get_name_np()``` или другой подобной, добавьте параметр ```WITH_PTHREAD_NP=0``` (по умолчанию он включен). При аналогичной ошибке с функцией ```setproctitle()``` добавьте параметр ```WITH_SETPROCTITLE=0```.

```
$ git clone --depth=1 https://github.com/pikvm/ustreamer
$ cd ustreamer
$ make
$ ./ustreamer --help
```

Для Arch Linux в AUR есть готовый пакет: https://aur.archlinux.org/packages/ustreamer. На Raspberry Pi програма автоматически собирается с поддержкой OpenMAX IL, если обнаружит нужные хедеры в ```/opt/vc/include```.  
Порт для FreeBSD: https://www.freshports.org/multimedia/ustreamer.

-----
# Использование
Будучи запущенным без аргументов, ```ustreamer``` попробует открыть устройство ```/dev/video0``` с разрешением 640x480 и начать трансляцию на ```http://127.0.0.1:8080```. Это поведение может быть изменено с помощью опций ```--device```, ```--host``` и ```--port```. Пример вещания на всю сеть по 80-м порту:
```
# ./ustreamer --device=/dev/video1 --host=0.0.0.0 --port=80
```

:exclamation: Обратите внимание, что начиная с версии µStreamer v2.0 кросс-доменные запросы были выключены по умолчанию [по соображениям безопасности](https://developer.mozilla.org/en-US/docs/Web/HTTP/CORS). Чтобы включить старое поведение, используйте опцию `--allow-origin=\*`.

Рекомендуемый способ запуска µStreamer для работы с [Auvidea B101](https://www.raspberrypi.org/forums/viewtopic.php?f=38&t=120702&start=400#p1339178) на Raspberry Pi:
```bash
$ ./ustreamer \
    --format=uyvy \ # Настройка входного формата устройства
    --encoder=omx \ # Использование аппаратного кодирования с помощью OpenMAX
    --workers=3 \ # Максимум воркеров для OpenMAX
    --persistent \ # Не переинициализировать устройство при таймауте (например, когда был отключен HDMI-кабель)
    --dv-timings \ # Включение DV-таймингов
    --drop-same-frames=30 # Экономим трафик
```

:exclamation: Обратите внимание, что для использования `--drop-same-frames` для разных браузеров нужно использовать ряд специальных параметров в `/stream` (за деталями обратитесь к урлу `/`).

За полным списком опций обращайтесь ко встроенной справке: ```ustreamer --help```.

-----
# Камера Raspberry Pi
Пример использования камеры Raspberry Pi v1:
```bash
$ sudo modprobe bcm2835-v4l2
$ ./ustreamer --host :: -m jpeg --device-timeout=5 --buffers=3 -r 2592x1944
```

:exclamation: Обратите внимание что боле новые модели камеры имеют другое максимальное разрешение. Список поддерживаемых разрешений можно найти в [документации PiCamera](https://picamera.readthedocs.io/en/release-1.13/fov.html#sensor-modes).

:exclamation: Если камера выдает низкий фреймрейт, возможно что она работает в фото-режиме, где производит более низкий фпс, но более качественную кратинку. Это происходит потому что `bcm2835-v4l2` переключает камеру в фото-режим на разрешениях выше `1280x720`. Чтобы обойти это, передайте параметры `max_video_width` и `max_video_height` при загрузке модуля, например:

```bash
$ modprobe bcm2835-v4l2 max_video_width=2592 max_video_height=1944
```

-----
# Смотрите также
* [Запуск с помощью systemd-сервиса](https://github.com/pikvm/ustreamer/issues/16).
* [uStreamer Ansible Role](https://github.com/mtlynch/ansible-role-ustreamer): Использование [Ansible](https://docs.ansible.com/ansible/latest/index.html) для сборки и установки стримера как systemd-сервиса.

-----
# Лицензия
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
