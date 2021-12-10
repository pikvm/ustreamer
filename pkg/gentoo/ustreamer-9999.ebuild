# Copyright 2019 Gentoo Authors
# Distributed under the terms of the GNU General Public License v2

EAPI=7

inherit git-r3

DESCRIPTION="uStreamer - Lightweight and fast MJPEG-HTTP streamer"
HOMEPAGE="https://github.com/pikvm/ustreamer"
EGIT_REPO_URI="https://github.com/pikvm/ustreamer.git"

LICENSE="GPL-3"
SLOT="0"
KEYWORDS="~amd64"
IUSE=""

DEPEND="
	>=dev-libs/libevent-2.1.8
	>=media-libs/libjpeg-turbo-1.5.3
	>=dev-libs/libbsd-0.9.1
"
RDEPEND="${DEPEND}"
BDEPEND=""

src_install() {
	dobin ustreamer
	dobin ustreamer-dump
	doman man/ustreamer.1
	doman man/ustreamer-dump.1
}
