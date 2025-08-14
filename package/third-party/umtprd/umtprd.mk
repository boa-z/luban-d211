################################################################################
#
# umtprd
#
################################################################################

UMTPRD_VERSION = 1.6.8
UMTPRD_SITE = https://github.com/viveris/uMTP-Responder/archive
UMTPRD_LICENSE = GPL-3.0+
UMTPRD_LICENSE_FILES = LICENSE

define UMTPRD_BUILD_CMDS
	$(TARGET_CONFIGURE_OPTS) $(MAKE) -C $(@D)
endef

define UMTPRD_INSTALL_TARGET_CMDS
	rm -f $(TARGET_DIR)/etc/init.d/S90adbd

	$(INSTALL) -d -m 0777 $(TARGET_DIR)/etc/umtprd
	$(INSTALL) -m 0777 $(@D)/$(MTP_UMTP_SUBDIR)/conf/umtprd.conf $(TARGET_DIR)/etc/umtprd/
	$(INSTALL) -m 0777 $(@D)/$(MTP_UMTP_SUBDIR)/conf/umtprd-ffs.sh $(TARGET_DIR)/usr/sbin/umtprd-ffs.sh
	$(INSTALL) -D -m 0755 $(@D)/umtprd $(TARGET_DIR)/usr/sbin/umtprd
endef

$(eval $(generic-package))
