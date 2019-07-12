# Contributor: Maxim Devaev <mdevaev@gmail.com>
# Author: Maxim Devaev <mdevaev@gmail.com>


pkgname=ustreamer
pkgver=0.81
pkgrel=1
pkgdesc="Lightweight and fast MJPG-HTTP streamer"
url="https://github.com/pi-kvm/ustreamer"
license=(GPL)
arch=(i686 x86_64 armv6h armv7h)
depends=(libjpeg libevent libutil-linux)
# optional: raspberrypi-firmware for OMX JPEG encoder
# optional: wiringpi for GPIO support
makedepends=(gcc make)
source=(${pkgname}::"git+https://github.com/pi-kvm/ustreamer#commit=v${pkgver}")
md5sums=(SKIP)


build() {
	cd "$srcdir"
	rm -rf $pkgname-build
	cp -r $pkgname $pkgname-build
	cd $pkgname-build

	local _options=""
	[ -e /opt/vc/include/IL/OMX_Core.h ] && _options="$_options WITH_OMX=1"
	[ -e /usr/include/wiringPi.h ] && _options="$_options WITH_GPIO=1"

	make $_options CFLAGS="$CFLAGS" LDFLAGS="$LDFLAGS" $MAKEFLAGS
}

package() {
	cd "$srcdir/$pkgname-build"
	make DESTDIR="$pkgdir" PREFIX=/usr install
}
