/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __REBOOT_REASON_H__
#define __REBOOT_REASON_H__

enum aic_reboot_reason {
	REBOOT_REASON_COLD = 0,
	REBOOT_REASON_CMD_REBOOT = 1,
	REBOOT_REASON_CMD_SHUTDOWN = 2,
	REBOOT_REASON_SUSPEND = 3,
	REBOOT_REASON_UPGRADE = 4,
	REBOOT_REASON_BL_UPGRADE = 5,

	/* Software exception reason, MUST begin with 10 */
	REBOOT_REASON_SW_LOCKUP = 10,
	REBOOT_REASON_HW_LOCKUP,
	REBOOT_REASON_PANIC,
	REBOOT_REASON_RAMDUMP,

	/* Hardware exception reason, MUST begin with 16 */
	REBOOT_REASON_RTC = 16,
	REBOOT_REASON_EXTEND,
	REBOOT_REASON_DM,
	REBOOT_REASON_OTP,
	REBOOT_REASON_UNDER_VOL,

	REBOOT_REASON_INVALID = 0xff,
};

/* Defined in ArtInChip WRI driver */

#if defined(CONFIG_RTC_DRV_ARTINCHIP_V01) || defined(CONFIG_ARTINCHIP_WRI)
void aic_set_reboot_reason(enum aic_reboot_reason reason);
enum aic_reboot_reason aic_get_reboot_reason(void);
#else
void __weak aic_set_reboot_reason(enum aic_reboot_reason reason)
{
	(void)reason; // Unused
}

enum aic_reboot_reason __weak aic_get_reboot_reason(void)
{
	return REBOOT_REASON_COLD;
}
#endif

#endif
