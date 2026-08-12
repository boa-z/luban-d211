################################################################################
#
# ogg
#
################################################################################

LIBVORBIS_VERSION = 1.3.7
LIBVORBIS_SITE = https://downloads.xiph.org/releases/vorbis
LIBVORBIS_LICENSE = BSD-3-Clause
LIBVORBIS_LICENSE_FILES = NOTICE

VORBIS_CONF_ENV = LIBS=-lm
LIBVORBIS_INSTALL_STAGING = YES


$(eval $(autotools-package))
