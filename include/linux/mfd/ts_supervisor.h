/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef __LINUX_MFD_TS_SUPERVISOR_H
#define __LINUX_MFD_TS_SUPERVISOR_H

struct ts_supervisor {
	struct regmap *regmap;
};

/* I2C Register addresses */
#define SUPER_MODEL         0
#define SUPER_REV_INFO      1
#define SUPER_ADC_CHAN_ADV  2
#define SUPER_FEATURES0     3
#define SUPER_CMDS          8
#define SUPER_FLAGS         16
#define SUPER_INPUTS        24
#define SUPER_REBOOT_REASON 32
#define SUPER_SILO_BASE     64

#define SUPER_ADC_BASE      128
#define SUPER_ADC_LAST      159
#define SUPER_TEMPERATURE   160
#define SUPER_CURRENT       161

#define WIZARD_IRQCHIP_BASE  512
#define WIZARD_SILO_IRQ_BASE 528

#define IRQ_STATUS      0
#define IRQ_ACK         1
#define IRQ_MASK_SET    2
#define IRQ_MASK_CLR    3

enum gen_flags_t {
	FLG_FORCE_USB_CON = BIT(4),
	FLG_LED_DAT = BIT(3),
	FLG_OVERRIDE_LED = BIT(2),
	FLG_WAKE_EN = BIT(1),
};

enum gen_inputs_t {
	INPUTS_USB_VBUS = BIT(0),
};

enum super_features_t {
	SUPER_FEAT_CT = BIT(6),        // Channel Table visible
	SUPER_FEAT_SILO = BIT(5),
	SUPER_FEAT_BOOT_MODE = BIT(4),
	SUPER_FEAT_RBTR = BIT(3),      // TBI on i.MX93
	SUPER_FEAT_SN = BIT(2),
	SUPER_FEAT_FWUPD = BIT(1),
	SUPER_FEAT_RSTC = BIT(0),
};

enum reboot_reasons_t {
	REBOOT_REASON_POR = 0,
	REBOOT_REASON_CPU_WDT = 1,
	REBOOT_REASON_SOFTWARE_REBOOT = 2,
	REBOOT_REASON_BROWNOUT = 3,
	REBOOT_REASON_RTC_ALARM_REBOOT = 4,
	REBOOT_REASON_WAKE_FROM_PWR_CYCLE = 5,
	REBOOT_REASON_WAKE_FROM_WAKE_SIGNAL = 6,
	REBOOT_REASON_WAKE_FROM_RTC_ALARM = 7,
	REBOOT_REASON_WAKE_FROM_USB_VBUS = 8,
};

enum super_cmds_t {
	I2C_CMD_RESERVED3 = BIT(3),
	I2C_CMD_RESERVED2 = BIT(2),
	I2C_HALT = BIT(1),
	I2C_REBOOT = BIT(0),
};

#endif
