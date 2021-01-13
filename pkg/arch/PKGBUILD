# Contributor: Maxim Devaev <mdevaev@gmail.com>
# Author: Maxim Devaev <mdevaev@gmail.com>


pkgname=ustreamer
pkgver=3.3
pkgrel=1
pkgdesc="Lightweight and fast MJPG-HTTP streamer"
url="https://github.com/pikvm/ustreamer"
license=(GPL)
arch=(i686 x86_64 armv6h armv7h aarch64)
depends=(libjpeg libevent libutil-linux libbsd libgpiod)
# optional: raspberrypi-firmware for OMX encoder
makedepends=(gcc make)
source=(${pkgname}::"git+https://github.com/pikvm/ustreamer#commit=v${pkgver}")
md5sums=(SKIP)


build() {
	cd "$srcdir"
	rm -rf $pkgname-build
	cp -r $pkgname $pkgname-build
	cd $pkgname-build

	local _options="WITH_GPIO=1"
	[ -e /opt/vc/include/IL/OMX_Core.h ] && _options="$_options WITH_OMX=1"

	make $_options CFLAGS="$CFLAGS" LDFLAGS="$LDFLAGS" $MAKEFLAGS
}

package() {
	cd "$srcdir/$pkgname-build"
	make DESTDIR="$pkgdir" PREFIX=/usr install
}
