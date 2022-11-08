# Contributor: Maxim Devaev <mdevaev@gmail.com>
# Author: Maxim Devaev <mdevaev@gmail.com>


pkgname=ustreamer
pkgver=5.31
pkgrel=1
pkgdesc="Lightweight and fast MJPEG-HTTP streamer"
url="https://github.com/pikvm/ustreamer"
license=(GPL)
arch=(i686 x86_64 armv6h armv7h aarch64)
depends=(libjpeg libevent libbsd libgpiod systemd)
makedepends=(gcc make systemd)
source=(${pkgname}::"git+https://github.com/pikvm/ustreamer#commit=v${pkgver}")
md5sums=(SKIP)


_options="WITH_GPIO=1 WITH_SYSTEMD=1"
if [ -e /usr/bin/python3 ]; then
	_options="$_options WITH_PYTHON=1"
	depends+=(python)
	makedepends+=(python-setuptools)
fi
if [ -e /usr/include/janus/plugins/plugin.h ];then
	depends+=(janus-gateway alsa-lib opus)
	makedepends+=(janus-gateway alsa-lib opus)
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
