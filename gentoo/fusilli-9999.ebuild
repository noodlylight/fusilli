# Copyright 1999-2015 Gentoo Foundation
# Distributed under the terms of the GNU General Public License v2
# $Header: $

EAPI=5

inherit autotools eutils git-2

DESCRIPTION="OpenGL Compositing Window Manager"
HOMEPAGE="http://github.com/noodlylight/fusilli"
EGIT_REPO_URI="git://github.com/noodlylight/fusilli.git"
SRC_URI=""

LICENSE="GPL-2 LGPL-2.1 MIT"
SLOT="0"
KEYWORDS="~amd64 ~x86"
IUSE="mate gtk kde +svg"

COMMONDEPEND="
	>=dev-libs/glib-2
	dev-libs/libxml2
	media-libs/libpng:0=
	>=media-libs/mesa-6.5.1-r1
	>=x11-base/xorg-server-1.1.1-r1
	>=x11-libs/libX11-1.4
	x11-libs/libxcb
	x11-libs/libXcomposite
	x11-libs/libXdamage
	x11-libs/libXinerama
	x11-libs/libXrandr
	x11-libs/libICE
	x11-libs/libSM
	>=x11-libs/libXrender-0.8.4
	>=x11-libs/startup-notification-0.7
	virtual/glu
	x11-libs/cairo[X]
	>=sys-apps/dbus-1.0
	dev-libs/dbus-glib
	mate? (
		>=mate-base/marco-1.8
	)
	gtk? (
		>=x11-libs/gtk+-2.8.0:2
		>=x11-libs/libwnck-2.18.3:1
		x11-libs/pango
	)
	kde? ( >=kde-base/kwin-4.2.0 )
	svg? (
		>=gnome-base/librsvg-2.14.0:2
		>=x11-libs/cairo-1.0
	)
"

DEPEND="${COMMONDEPEND}
	virtual/pkgconfig
	x11-proto/damageproto
	x11-proto/xineramaproto
"

RDEPEND="${COMMONDEPEND}
	x11-apps/mesa-progs
	x11-apps/xdpyinfo
	x11-apps/xset
	x11-apps/xvinfo
"

DOCS=( AUTHORS README.md TODO )

src_prepare() {
	eautoreconf
}

src_configure() {
	local myconf

	econf \
		--enable-fast-install \
		--disable-static \
		$(use_enable svg librsvg) \
		$(use_enable mate marco) \
		$(use_enable gtk) \
		$(use_enable kde kde4) \
		${myconf}
}

src_install() {
	default
	prune_libtool_files --all

	domenu "${FILESDIR}"/fusilli.desktop
}


