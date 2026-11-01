SELFIE_VERSION =
SELFIE_ENABLE_TARBALL = NO
SELFIE_ENABLE_PATCH = NO

SELFIE_DEPENDENCIES += test-common
SELFIE_DEPENDENCIES += libzlib libglib2
SELFIE_DEPENDENCIES += dbus bluez5_utils bluez5_utils-headers sbc

SELFIE_CONF_OPTS += -DCMAKE_INSTALL_PREFIX=/usr/local -DSTAGING_DIR=$(STAGING_DIR)

define SELFIE_INSTALL_INIT_SYSV
	$(INSTALL) -m 0755 -D package/artinchip/selfie/S60selfie \
		$(TARGET_DIR)/etc/init.d/S60selfie
endef

$(eval $(cmake-package))
