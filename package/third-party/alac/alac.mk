################################################################################
#
# alac
#
################################################################################

ALAC_VERSION = 1.0
ALAC_SITE = http://downloads.sourceforge.net/project/opencore-amr/fdk-aac
ALAC_LICENSE = alac license
ALAC_LICENSE_FILES = NOTICE

ALAC_INSTALL_STAGING = YES

$(eval $(cmake-package))


