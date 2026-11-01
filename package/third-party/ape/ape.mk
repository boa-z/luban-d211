################################################################################
#
# ape (APE decoder library from Rockbox demac)
#
################################################################################

APE_VERSION = master
APE_SITE = $(call github,Rockbox,rockbox)
APE_SITE_METHOD = git
APE_SOURCE = rockbox-$(APE_VERSION).tar.gz
APE_SUBDIR = lib/rbcodec/codecs/demac

APE_LICENSE = GPL-2.0
APE_LICENSE_FILES = COPYING

APE_CONF_ENV = LIBS=-lm
APE_INSTALL_STAGING = YES
APE_INSTALL_TARGET = YES

$(eval $(cmake-package))