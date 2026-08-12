################################################################################
#
# flac
#
################################################################################

FLAC_VERSION = 1.4.3
FLAC_SITE = $(call github,xiph,flac,$(FLAC_VERSION))
FLAC_SOURCE = flac-$(FLAC_VERSION).tar.gz
FLAC_LICENSE = BSD-3-Clause
FLAC_LICENSE_FILES = COPYING.Xiph
FLAC_INSTALL_STAGING = YES
FLAC_INSTALL_TARGET = YES
FLAC_CONF_OPTS = \
	-DBUILD_SHARED_LIBS=OFF \
	-DBUILD_PROGRAMS=OFF \
	-DBUILD_EXAMPLES=OFF \
	-DBUILD_TESTING=OFF \
	-DBUILD_DOCS=OFF \
	-DINSTALL_MANPAGES=OFF \
	-DCMAKE_POSITION_INDEPENDENT_CODE=ON

$(eval $(cmake-package))
