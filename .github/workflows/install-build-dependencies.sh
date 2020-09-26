#!/bin/bash
set -euxo pipefail

# add the raspberrypi repository.
apt-get update
apt-get install -y --no-install-recommends gpg gpg-agent dirmngr
gpg --keyserver keys.gnupg.net --recv-key 82B129927FA3303E
gpg --armor --export 82B129927FA3303E | apt-key add -
echo 'deb http://archive.raspberrypi.org/debian/ buster main' >/etc/apt/sources.list.d/raspi.list
apt-get update

# install build dependencies.
apt-get install -y --no-install-recommends \
    gcc \
    libbsd-dev \
    libevent-dev \
    libgpiod-dev \
    libjpeg62-turbo-dev \
    make \
    uuid-dev
if [[ "$TARGETPLATFORM" == linux/arm* ]]; then
    apt-get install -y --no-install-recommends \
        libraspberrypi-dev
    WITH_OMX='1'
else
    WITH_OMX='0'
fi

# save the build command line.
cat >build.sh <<EOF
#!/bin/bash
set -euxo pipefail
make -j5 WITH_OMX=$WITH_OMX WITH_GPIO=1
EOF
chmod +x build.sh

# clean up.
rm -rf /var/lib/apt/lists/*
