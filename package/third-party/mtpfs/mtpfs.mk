################################################################################
#
# mtpfs
#
################################################################################

MTPFS_VERSION = 1.0
MTPFS_SOURCE = mtpfs-$(MTPFS_VERSION).tar.gz
MTPFS_SITE = https://github.com/cjd/mtpfs/archive/refs/tags/v$(MTPFS_VERSION).tar.gz
MTPFS_LICENSE = LGPL-2.1+
MTPFS_LICENSE_FILES = COPYING
MTPFS_DEPENDENCIES = host-automake host-autoconf host-libtool libmtp libfuse
MTPFS_SUBDIR = mtpfs-$(MTPFS_VERSION)

define MTPFS_CONFIGURE_CMDS
    cd $(@D) && \
    chmod +x autogen.sh && \
    ./autogen.sh && \
    ./configure \
        --prefix=/usr \
        --host=$(GNU_TARGET_NAME) \
        --build=$(GNU_HOST_NAME) \
        --disable-static \
        --enable-shared \
        --disable-dependency-tracking \
		--disable-mad \
		--disable-id3tag \
        $(DISABLE_NLS) \
        $(TARGET_CONFIGURE_OPTS)
endef

define MTPFS_BUILD_CMDS
    $(MAKE) -C $(@D) \
        $(TARGET_CONFIGURE_OPTS)
endef

define MTPFS_INSTALL_STAGING_CMDS
    $(MAKE) -C $(@D) install \
        DESTDIR=$(STAGING_DIR)
endef

define MTPFS_INSTALL_TARGET_CMDS
    $(MAKE) -C $(@D) install \
        DESTDIR=$(TARGET_DIR)
endef

$(eval $(generic-package))