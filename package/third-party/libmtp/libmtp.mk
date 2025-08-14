################################################################################
#
# libmtp
#
################################################################################

LIBMTP_VERSION = 1.1.22
LIBMTP_SOURCE = libmtp-$(LIBMTP_VERSION).tar.gz
LIBMTP_SITE = https://sourceforge.net/projects/libmtp/files/libmtp/$(LIBMTP_VERSION)
LIBMTP_SHA256 = c3fcf411aea9cb9643590cbc9df99fa5fe30adcac695024442973d76fa5f87bc
LIBMTP_LICENSE = LGPL-2.1+
LIBMTP_LICENSE_FILES = COPYING
LIBMTP_INSTALL_STAGING = YES
LIBMTP_DEPENDENCIES = host-pkgconf libusb

LIBMTP_PRE_CONFIGURE_HOOKS += LIBMTP_REMOVE_HOTPLUG_INSTALL_HOOK

$(eval $(autotools-package))