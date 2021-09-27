# Contributor: Maxim Devaev <mdevaev@gmail.com>
# Author: Maxim Devaev <mdevaev@gmail.com>


pkgname=ustreamer
pkgver=4.6
pkgrel=1
pkgdesc="Lightweight and fast MJPG-HTTP streamer"
url="https://github.com/pikvm/ustreamer"
license=(GPL)
arch=(i686 x86_64 armv6h armv7h aarch64)
depends=(libjpeg libevent libbsd libgpiod)
makedepends=(gcc make)
source=(${pkgname}::"git+https://github.com/pikvm/ustreamer#commit=v${pkgver}")
md5sums=(SKIP)


_options="WITH_GPIO=1"
if [ -e /usr/bin/python3 ]; then
	_options="$_options WITH_PYTHON=1"
	depends+=(python)
	makedepends+=(python-setuptools)
fi
if [ -e /opt/vc/include/IL/OMX_Core.h ]; then
	depends+=(raspberrypi-firmware)
	makedepends+=(raspberrypi-firmware)
	_options="$_options WITH_OMX=1"
fi
if [ -e /usr/include/janus/plugins/plugin.h ];then
	depends+=(janus-gateway-pikvm)
	makedepends+=(janus-gateway-pikvm)
	_options="$_options WITH_JANUS=1"
fi


# LD does not link mmal with this option
# This DOESN'T affect setup.py
LDFLAGS="${LDFLAGS//--as-needed/}"
export LDFLAGS="${LDFLAGS//,,/,}"


build() {
	cd "$srcdir"
	rm -rf $pkgname-build
	cp -r $pkgname $pkgname-build
	cd $pkgname-build
	make $_options CFLAGS="$CFLAGS" LDFLAGS="$LDFLAGS" $MAKEFLAGS
}

package() {
	cd "$srcdir/$pkgname-build"
	make $_options CFLAGS="$CFLAGS" LDFLAGS="$LDFLAGS" DESTDIR="$pkgdir" PREFIX=/usr install
}
