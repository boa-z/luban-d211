# SPDX-License-Identifier:	GPL-2.0

obj-y += upg_main.o
obj-y += basic_cmd.o
obj-y += fwc_cmd.o
obj-y += ram_fwc.o
obj-y += spi_enc_spl.o
obj-y += upg_sdcard.o upg_fat.o
obj-$(CONFIG_AICUPG_NAND_ARTINCHIP) += nand_fwc.o
obj-$(CONFIG_AICUPG_NOR_ARTINCHIP) += nor_fwc.o
obj-$(CONFIG_AICUPG_MMC_ARTINCHIP) += mmc_fwc.o

ifneq ($(wildcard $(srctree)/drivers/aicupg/libfirmware_security.a),)
$(shell cp $(srctree)/drivers/aicupg/libfirmware_security.a $(obj)/libfirmware_security.a)
obj-$(CONFIG_AICUPG_FIRMWARE_SECURITY) += libfirmware_security.a
else
quiet_cmd_ar = AR $@
	cmd_ar = $(AR) crs $@ $^

$(obj)/libfirmware_security.a: $(obj)/firmware_security.o
	$(Q)$(AR) crs $@ $^

obj-$(CONFIG_AICUPG_FIRMWARE_SECURITY) += libfirmware_security.a
endif

ifdef CONFIG_TARGET_D211
obj-$(CONFIG_AICUPG_NAND_ARTINCHIP) += nand_fwc_spl.o
endif
