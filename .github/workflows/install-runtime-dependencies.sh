#!/bin/bash
set -euxo pipefail

# add the raspberrypi repository.
apt-get update
apt-get install -y --no-install-recommends gpg gpg-agent dirmngr
gpg --keyserver keys.gnupg.net --recv-key 82B129927FA3303E
gpg --armor --export 82B129927FA3303E | apt-key add -
echo 'deb http://archive.raspberrypi.org/debian/ buster main' >/etc/apt/sources.list.d/raspi.list
apt-get update

# install runtime dependencies.
apt-get install -y --no-install-recommends \
    libbsd0 \
    libevent-2.1 \
    libevent-pthreads-2.1-6 \
    libgpiod2 \
    libjpeg62-turbo \
    libuuid1
if [[ "$TARGETPLATFORM" == linux/arm* ]]; then
    apt-get install -y --no-install-recommends \
        libraspberrypi0
fi

# clean up.
apt-get remove -y --purge gpg gpg-agent dirmngr
apt-get autoremove -y --purge
apt-get clean
rm -rf /var/lib/apt/lists/*
