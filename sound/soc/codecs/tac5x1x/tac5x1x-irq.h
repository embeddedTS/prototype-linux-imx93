/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Audio Codec Driver Supporting Devices
 * TAA5X1X, TAC5X1X, TAD5X1X
 *
 * Copyright (C) 2024-2025 Texas Instruments Incorporated - https://www.ti.com
 *
 * Author: Kokila Karuppusamy <kokila.karuppusamy@ti.com>
 * Author: Kevin Lu <kevin-lu@ti.com>
 */
#ifndef __TAC5X1X_IRQ_REG_H__
#define __TAC5X1X_IRQ_REG_H__

#define	TAC5X1X_REG_INT_LTCH0		TAC5X1X_REG(0x1, 0x34)
#define	TAC5X1X_REG_CHX_LTCH		TAC5X1X_REG(0x1, 0x35)
#define	TAC5X1X_REG_IN_CH1_LTCH		TAC5X1X_REG(0x1, 0x36)
#define	TAC5X1X_REG_IN_CH2_LTCH		TAC5X1X_REG(0x1, 0x37)
#define	TAC5X1X_REG_OUT_CH1_LTCH	TAC5X1X_REG(0x1, 0x38)
#define	TAC5X1X_REG_OUT_CH2_LTCH	TAC5X1X_REG(0x1, 0x39)
#define	TAC5X1X_REG_INT_LTCH1		TAC5X1X_REG(0x1, 0x3A)
#define	TAC5X1X_REG_INT_LTCH2		TAC5X1X_REG(0x1, 0x3B)

struct interrupt_info {
	const char **names;
	size_t size;
};

void tac5x1x_irq_work_func(struct tac5x1x_priv *tac5x1x);

#endif
