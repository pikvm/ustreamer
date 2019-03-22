# Copyright 2019 Gentoo Authors
# Distributed under the terms of the GNU General Public License v2
 
EAPI=7
 
inherit git-r3
 
DESCRIPTION="µStreamer - Lightweight and fast MJPG-HTTP streamer"
HOMEPAGE="https://github.com/pi-kvm/ustreamer"
EGIT_REPO_URI="https://github.com/pi-kvm/ustreamer.git"
 
LICENSE="GPL-3"
SLOT="0"
KEYWORDS="~amd64"
IUSE=""
 
DEPEND="
	>=dev-libs/libevent-2.1.8
	>=media-libs/libjpeg-turbo-1.5.3
	>=sys-apps/util-linux-2.33
"
RDEPEND="${DEPEND}"
BDEPEND=""
 
src_install() {
	dobin ustreamer 
}
