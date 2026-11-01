################################################################################
#
# ogg
#
################################################################################

LIBOGG_VERSION = 1.3.6
LIBOGG_SITE = https://downloads.xiph.org/releases/ogg
LIBOGG_LICENSE = BSD-3-Clause
LIBOGG_LICENSE_FILES = NOTICE

LIBOGG_INSTALL_STAGING = YES


$(eval $(autotools-package))
