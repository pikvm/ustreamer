# Contributor: Maxim Devaev <mdevaev@gmail.com>
# Author: Maxim Devaev <mdevaev@gmail.com>


pkgname=ustreamer
pkgver=0.45
pkgrel=1
pkgdesc="Lightweight and fast MJPG-HTTP streamer"
url="https://github.com/pi-kvm/ustreamer"
license=(GPL)
arch=(i686 x86_64 armv6h armv7h)
depends=(libjpeg libevent libutil-linux)
# optional: raspberrypi-firmware for OMX JPEG compressor
makedepends=(gcc make)
source=("$url/archive/v$pkgver.tar.gz")
md5sums=(SKIP)


build() {
	cd $srcdir
	rm -rf $pkgname-build
	cp -r ustreamer-$pkgver $pkgname-build
	cd $pkgname-build
	make CFLAGS="$CFLAGS" LDFLAGS="$LDFLAGS" $MAKEFLAGS
}

package() {
	cd $srcdir/$pkgname-build
	make DESTDIR="$pkgdir" PREFIX=/usr install
}
