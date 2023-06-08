#
# This is free software, licensed under the GNU General Public License v2.
# See /LICENSE for more information.
#

include $(TOPDIR)/rules.mk

PKG_NAME:=ustreamer
PKG_VERSION:=5.40
PKG_RELEASE:=1
PKG_MAINTAINER:=Maxim Devaev <mdevaev@gmail.com>

PKG_SOURCE_PROTO:=git
PKG_SOURCE_URL:=https://github.com/pikvm/ustreamer.git
PKG_SOURCE_VERSION:=v$(PKG_VERSION)
PKG_SOURCE:=$(PKG_NAME)-$(PKG_VERSION).tar.xz
PKG_SOURCE_SUBDIR:=$(PKG_NAME)-$(PKG_VERSION)

PKG_LICENSE:=GPL-3.0
PKG_LICENSE_FILES:=LICENSE

include $(INCLUDE_DIR)/package.mk

define Package/ustreamer
  SECTION:=multimedia
  CATEGORY:=Multimedia
  TITLE:=uStreamer
  DEPENDS:=+libpthread +libjpeg +libv4l +libbsd +libevent2 +libevent2-core +libevent2-extra +libevent2-pthreads
  URL:=https://github.com/pikvm/ustreamer
endef

define Package/ustreamer/description
 µStreamer - Lightweight and fast MJPEG-HTTP streamer
endef

define Package/ustreamer/install
	$(INSTALL_DIR) $(1)/usr/bin
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/ustreamer $(1)/usr/bin/
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/ustreamer-dump $(1)/usr/bin/
	$(INSTALL_DIR) $(1)/etc/config
	$(CP) ./files/ustreamer.config $(1)/etc/config/ustreamer
	$(INSTALL_DIR) $(1)/etc/init.d
	$(INSTALL_BIN) ./files/ustreamer.init $(1)/etc/init.d/ustreamer
endef

$(eval $(call BuildPackage,ustreamer))
