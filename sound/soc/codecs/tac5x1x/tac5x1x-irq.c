/* SPDX-License-Identifier: GPL-2.0
 * TAC5X1X codec driver
 *
 * Copyright (C) 2020-2025 Texas Instruments Incorporated
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#include <linux/firmware.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <sound/soc.h>

#include "tac5x1x.h"
#include "tac5x1x-irq.h"

static const char *int_ltch0[] = {
	"Clock Error",
	"PLL Lock",
	"Boost Over Temperature",
	"Boost Over Current",
	"Boost Mode",
	"Reserved",
	"Reserved",
	"Reserved",
};

static const char *chx_ltch[] = {
	"Input Channel1 fault",
	"Input Channel2 fault",
	"Output Channel1 fault",
	"Output Channel2 fault",
	"Short to VBAT_IN",
	"Reserved",
	"Reserved",
	"Reserved",
};

static const char *in_ch1_ltch[] = {
	"IN_CH1 open Input",
	"IN_CH1 Input shorted",
	"IN_CH1 INP shorted to GND",
	"IN_CH1 INM shorted to GND",
	"IN_CH1 INP shorted to MICBIAS",
	"IN_CH1 INM shorted to MICBIAS",
	"IN_CH1 INP shorted to VBAT_IN",
	"IN_CH1 INM shorted to VBAT_IN",
};

static const char *in_ch2_ltch[] = {
	"IN_CH2 open Input",
	"IN_CH2 Input shorted",
	"IN_CH2 INP shorted to GND",
	"IN_CH2 INM shorted to GND",
	"IN_CH2 INP shorted to MICBIAS",
	"IN_CH2 INM shorted to MICBIAS",
	"IN_CH2 INP shorted to VBAT_IN",
	"IN_CH2 INM shorted to VBAT_IN",
};

static const char *out_ch1_ltch[] = {
	"OUT_CH1 OUT1P Short circuit Fault",
	"OUT_CH1 OUT1M Short circuit Fault",
	"OUT_CH1 DRVRP Virtual Ground Fault",
	"OUT_CH1 DRVRM Virtual ground Fault",
	"OUT_CH1 ADC CH1 Mask",
	"OUT_CH1 ADC CH2 MASK",
	"Reserved",
	"Reserved",
};

static const char *out_ch2_ltch[] = {
	"OUT_CH2 OUT2P Short circuit Fault",
	"OUT_CH2 OUT2M Short circuit Fault",
	"OUT_CH2 DRVRP Virtual Ground Fault",
	"OUT_CH2 DRVRM Virtual ground Fault",
	"Reserved",
	"Reserved",
	"AREG SC Fault Mask",
	"AREG SC Fault",
};

static const char *int_ltch1[] = {
	"CH1 INP Over Voltage",
	"CH1 INM Over Voltage",
	"CH2 INP over Voltage",
	"CH2 INM Over Voltage",
	"Headset Insert Detection",
	"Headset Remove Detection",
	"Headset Hook",
	"MIPS Overload",
};

static const char *int_ltch2[] = {
	"GPA Up threashold Fault",
	"GPA low threashold Fault",
	"VAD Power up detect",
	"VAD power down detect",
	"Micbias short circuit",
	"Micbias high current fault",
	"Micbias low current fault",
	"Micbias Over voltage fault",
};

unsigned int int_reg_array[] = {
	TAC5X1X_REG_INT_LTCH0,
	TAC5X1X_REG_CHX_LTCH,
	TAC5X1X_REG_IN_CH1_LTCH,
	TAC5X1X_REG_IN_CH2_LTCH,
	TAC5X1X_REG_OUT_CH1_LTCH,
	TAC5X1X_REG_OUT_CH2_LTCH,
	TAC5X1X_REG_INT_LTCH1,
	TAC5X1X_REG_INT_LTCH2,
};

static const char *int_reg_arr_name[] = {
	"TAC5X1X_REG_INT_LTCH0",
	"TAC5X1X_REG_CHX_LTCH",
	"TAC5X1X_REG_IN_CH1_LTCH",
	"TAC5X1X_REG_IN_CH2_LTCH",
	"TAC5X1X_REG_OUT_CH1_LTCH",
	"TAC5X1X_REG_OUT_CH2_LTCH",
	"TAC5X1X_REG_INT_LTCH1",
	"TAC5X1X_REG_INT_LTCH2",

};

static struct interrupt_info interrupts[] = {
	{ int_ltch0, ARRAY_SIZE(int_ltch0) },
	{ chx_ltch, ARRAY_SIZE(chx_ltch) },
	{ in_ch1_ltch, ARRAY_SIZE(in_ch1_ltch) },
	{ in_ch2_ltch, ARRAY_SIZE(in_ch2_ltch) },
	{ out_ch1_ltch, ARRAY_SIZE(out_ch1_ltch) },
	{ out_ch2_ltch, ARRAY_SIZE(out_ch2_ltch) },
	{ int_ltch1, ARRAY_SIZE(int_ltch1) },
	{ int_ltch2, ARRAY_SIZE(int_ltch2) },
};

int tac5x1x_regmap_write(
		struct tac5x1x_priv *tac5x1x,
		unsigned int reg, unsigned int value)
{
	int retry_count = 5;
	int ret;

	while (retry_count--) {
		ret = regmap_write(tac5x1x->regmap, reg,
				value);
		if (ret >= 0)
			break;
		usleep_range(5000, 5050);
	}
	if (retry_count == -1)
		return 3;
	else
		return ret;
}

static int tac5x1x_regmap_read(
		struct tac5x1x_priv *tac5x1x,
		unsigned int reg, unsigned int *value)
{
	int retry_count = 5;
	int ret;

	ret = regmap_reinit_cache(tac5x1x->regmap, &tac5x1x_regmap);
	if (ret) {
		dev_err(tac5x1x->dev, "Failed to reinit reg cache\n");
		return ret;
	}

	while (retry_count--) {
		ret = regmap_read(tac5x1x->regmap, reg,
				value);
		if (ret >= 0)
			break;
		usleep_range(5000, 5050);
	}
	if (retry_count == -1)
		return 3;
	else
		return ret;
}

int tac5x1x_dev_read(struct tac5x1x_priv *tac5x1x,
		unsigned int dev_no, unsigned int reg, unsigned int *pValue)
{
	int ret;

	guard(mutex)(&tac5x1x->dev_lock);
	if (dev_no < tac5x1x->ndev) {
		ret = tac5x1x_regmap_write(tac5x1x,
				TAC_PAGE_SELECT, 0);
		if (ret < 0) {
			dev_err(tac5x1x->dev, "%s, E=%d\n",
					__func__, ret);
			return ret;
		}

		ret = tac5x1x_regmap_read(tac5x1x, reg, pValue);
		if (ret < 0)
			dev_err(tac5x1x->dev, "read, ERROR, E=%d\n",
					ret);
		else
			dev_dbg(tac5x1x->dev,
					"read PAGE:REG 0x%02x:0x%02x,0x%02x\n",
					TAC_PAGE_ID(reg),
					TAC_PAGE_REG(reg), *pValue);
	} else {
		dev_err(tac5x1x->dev, "%s, ERROR: no such device(%d)\n",
				__func__, dev_no);
	}

	return 0;
}

void tac5x1x_irq_work_func(struct tac5x1x_priv *tac5x1x)
{
	unsigned int reg_val, array_size, i, index = 0, bit = 0;
	int rc;

	dev_info(tac5x1x->dev, "%s entered\n", __func__);
	tac5x1x_enable_irq(tac5x1x, false);
	array_size =  ARRAY_SIZE(int_reg_array);
	for (i = 0; i < array_size; i++) {
		rc = tac5x1x_dev_read(tac5x1x, index,
				int_reg_array[i], &reg_val);
		if (!rc) {
			for (int j = 0; j < interrupts[i].size; j++) {
				bit = interrupts[i].size - 1 - j;
				if (reg_val & (1 << bit))
					dev_info(tac5x1x->dev,
						"Reg: %s || %s\n",
						int_reg_arr_name[i],
						interrupts[i].names[j]);
			}
		} else {
			dev_err(tac5x1x->dev,
					"%s DEV_NO%d Read Reg 0x%04x error(rc=%d)\n",
					tac5x1x->dev_name, index,
					int_reg_array[i], rc);
		}
	}
	tac5x1x_enable_irq(tac5x1x, true);
}

MODULE_AUTHOR("Kokila Karuppusamy <kokila.karuppusamy@ti.com>");
MODULE_AUTHOR("Kevin Lu <kevin-lu@ti.com>");
MODULE_DESCRIPTION("TAC5x1x Stereo Audio codec");
MODULE_LICENSE("GPL");
