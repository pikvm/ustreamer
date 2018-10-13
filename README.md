# µStreamer

µStreamer - это маленький и очень быстрый сервер, который позволяет организовать трансляцию видео в формате [MJPG](https://en.wikipedia.org/wiki/Motion_JPEG) с любого устройства V4L2 в сеть. Этот формат нативно поддерживается всеми современными браузерами и большинством приложений для просмотра видео (mplayer, VLC и так далее). µStreamer был разработан в рамках проекта [Pi-KVM](https://github.com/pi-kvm) специально для стриминга с устройств видеозахвата [VGA](https://www.amazon.com/dp/B0126O0RDC) и [HDMI](https://auvidea.com/b101-hdmi-to-csi-2-bridge-15-pin-fpc/) с максимально возможным разрешением и FPS, которые только позволяет железо.

Функционально µStreamer очень похож на [mjpg-streamer](https://github.com/jacksonliam/mjpg-streamer) при использовании им плагинов ```input_uvc.so``` и ```output_http.so```, однако имеет ряд серьезных отличий. Основные приведены в этой таблице:

| **Фича** | **µStreamer** | **mjpg-streamer** |
|----------|---------------|-------------------|
| Многопоточное кодирование JPEG | ![#00aa00](https://placehold.it/15/00aa00/000000?text=+) Есть | ![#f03c15](https://placehold.it/15/f03c15/000000?text=+) Нет |
| Аппаратное кодирование с помощью [OpenMAX IL](https://www.khronos.org/openmaxil) на Raspberry Pi | ![#00aa00](https://placehold.it/15/00aa00/000000?text=+) Есть | ![#f03c15](https://placehold.it/15/f03c15/000000?text=+) Нет |
| Поведение при физическом отключении устройства<br>от сервера во время работы | ![#00aa00](https://placehold.it/15/00aa00/000000?text=+) Транслирует черный экран<br>с надписью ```NO SIGNAL```,<br>пока устройство не будет подключено снова | ![#f03c15](https://placehold.it/15/f03c15/000000?text=+) Необратимо зависает <sup>1</sup> |
| Поддержка [DV-таймингов](https://linuxtv.org/downloads/v4l-dvb-apis/uapi/v4l/dv-timings.html) - возможности изменения <br>параметров разрешения трансляции на лету<br>по сигналу источника (устройства видеозахвата) | ![#00aa00](https://placehold.it/15/00aa00/000000?text=+) Есть | ![#f03c15](https://placehold.it/15/f03c15/000000?text=+) Нет <sup>1</sup> |
| Возможность пропуска фреймов при передаче<br>статического изображения по HTTP<br>для экономии трафика | ![#00aa00](https://placehold.it/15/00aa00/000000?text=+) Есть <sup>2</sup> | ![#f03c15](https://placehold.it/15/f03c15/000000?text=+) Нет |
| Дебаг-логи без перекомпиляции,<br>логгирование статистики производительности,<br>возможность получения параметров<br>трансляции по HTTP | ![#00aa00](https://placehold.it/15/00aa00/000000?text=+) Есть | ![#f03c15](https://placehold.it/15/f03c15/000000?text=+) Нет |
| Поддерживаемые входные форматы устройств | ![#ffaa00](https://placehold.it/15/ffaa00/000000?text=+) YUYV, UYVY,<br>RGB565, ~~MJPG~~ <sup>3</sup> | ![#00aa00](https://placehold.it/15/00aa00/000000?text=+) YUYV, UYVY,<br>RGB565, MJPG |
| Поддержка контролов веб-камер (фокус,<br> движение сервами) и всяких настроек,<br> типа яркости, через HTTP | ![#f03c15](https://placehold.it/15/f03c15/000000?text=+) Нет | ![#00aa00](https://placehold.it/15/00aa00/000000?text=+) Есть |
| Возможность сервить файлы встроенным<br>HTTP-сервером, настройки авторизации | ![#f03c15](https://placehold.it/15/f03c15/000000?text=+) Нет <sup>4</sup> | ![#00aa00](https://placehold.it/15/00aa00/000000?text=+) Есть |

Сносочки:
  * ```1``` Для mjpg-streamer существует [мой патч](https://github.com/jacksonliam/mjpg-streamer/pull/164), предотвращающий зависание при отключении устройства и добавляющий поддержку DV-таймингов, однако трансляция при этом все равно прерывается. В данный момент этот патч не принят в апстрим, и я даже не гарантирую его стопроцентную работоспособность. Код mjpg-streamer очень плохо структурирован, чрезвычайно запутан, и я мог что-то упустить. Собственно, это одна из причин, почему µStreamer был написан с нуля.
  
  * ```2``` Это фича позволяет в несколько раз снизить объем исходящего трафика при трансляции HDMI, однако немного увеличивает загрузку процессора. Суть в том, что HDMI - полностью цифровой интерфейс, и новый захваченный фрейм может быть идентичен предыдущему в точности до байта. В этом случае нет нужды передавать одну и ту же картинку по сети несколько раз в секунду. При использовании опции `--drop-same-frames=20`, µStreamer будет дропать все одинаковые фреймы, но не более 20 подряд. Новый фрейм сравнивается с предыдущим сначала по длине, а затем помощью ```memcmp()```.

  * ```3``` Поскольку µStreamer писался в первую очередь для устройств видеозахвата, в нем реализованы только те форматы, которые для них были нужны. MJPG в контексте входных данных означает, что устройство умеет самостоятельно сжимать картинку в JPEG и отдавать ее программе, что позволяет значительно снизить загрузку процессора и избавить его от необходимости кодировать картинку софтом. Этот формат поддерживается большинством веб-камер, но не поддерживается ни одним из встреченных мной устройств видеозахвата; его не умеет ни [Auvidea B101](https://auvidea.com/b101-hdmi-to-csi-2-bridge-15-pin-fpc/), ни [EasyCap UTV 007](https://www.amazon.com/dp/B0126O0RDC). Нет никаких технических сложностей добавить поддержку аппаратного MJPG источника, но у меня просто пока не дошли до этого руки.

  * ```4``` ... и не будет. µStreamer придерживается концепции UNIX-way, так что если вам нужно нарисовать маленький сайтик со встроенной трансляцией - просто поставьте NGINX.

-----
# TL;DR
Если вам нужно вещать стрим с уличной камеры и управлять ее параметрами - возьмите mjpg-streamer. Если же вам нужно очень качественное изображение с высоким FPS - µStreamer ваш бро.

-----
# Сборка
Для сборки вам понадобятся ```make```, ```gcc```, ```libevent``` с поддержкой ```pthreads```, ```libjpeg8```/```libjpeg-turbo``` и ```libuuid```.

На Raspberry Pi програма автоматически собирается с поддержкой OpenMAX IL, если обнаружит нужные хедеры в ```/opt/vc/include```.

```
$ git clone --depth=1 https://github.com/pi-kvm/ustreamer
$ cd ustreamer
$ make
$ ./ustreamer --help
```

Для Arch Linux в AUR есть готовый пакет: https://aur.archlinux.org/packages/ustreamer

-----
# Использование
Будучи запущенным без аргументов, ```ustreamer``` попробует открыть устройство ```/dev/video0``` с разрешением 640x480 и начать трансляцию на ```http://localhost:8080```. Это поведение может быть изменено с помощью опций ```--device```, ```--host``` и ```--port```. Пример вещания на всю сеть по 80-м порту:
```
# ./ustreamer --device=/dev/video1 --host=0.0.0.0 --port=80
```

Рекомендуемый способ запуска µStreamer для работы с [Auvidea B101](https://www.raspberrypi.org/forums/viewtopic.php?f=38&t=120702&start=400#p1339178) на Raspberry Pi:
```bash
$ ./ustreamer \
    --format=uyvy \ # Настройка входного формата устройства
    --encoder=omx \ # Использование аппаратного кодирования с помощью OpenMAX
    --dv-timings \ # Включение DV-таймингов
    --quality=20 \ # У OpenMAX нелинейная шкала качества
    --drop-same-frames=30 # Экономим трафик
```

За полным списком опций обращайтесь ко встроенной справке: ```ustreamer --help```.

-----
# Лицензия
Copyright (C) 2018 by Maxim Devaev mdevaev@gmail.com

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
