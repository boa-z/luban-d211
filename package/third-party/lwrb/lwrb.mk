################################################################################
#
# lwrb
#
################################################################################

LWRB_VERSION = v3.2.0
LWRB_SOURCE = lwrb-$(LWRB_VERSION).tar.gz
LWRB_SITE = https://github.com/MaJerle/lwrb
LWRB_LICENSE = MIT
LWRB_LICENSE_FILES = LICENSE

LWRB_INSTALL_STAGING = YES
LWRB_INSTALL_TARGET = YES

LWRB_CONF_OPTS = \
	-DPROJECT_IS_TOP_LEVEL=OFF \
	-DBUILD_DOC=OFF \
	-DBUILD_DOCS=OFF \
	-DBUILD_EXAMPLE=OFF \
	-DBUILD_EXAMPLES=OFF \
	-DBUILD_TEST=OFF \
	-DBUILD_TESTING=OFF \
	-DBUILD_TESTS=OFF \
	-DCMAKE_INSTALL_PREFIX="/usr"

define LWRB_REMOVE_CMAKE_CACHE
	rm -f $(@D)/CMakeCache.txt
endef
LWRB_PRE_CONFIGURE_HOOKS += LWRB_REMOVE_CMAKE_CACHE

$(eval $(cmake-package))
