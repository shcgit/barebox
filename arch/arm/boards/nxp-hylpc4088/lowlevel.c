/*
 * Copyright (C) 2014 Alexander Shiyan <shc_work@mail.ru>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 */

#include <common.h>
#include <debug_ll.h>
#include <io.h>
#include <asm/barebox-arm.h>
#include <asm/barebox-arm-head.h>
#include <linux/sizes.h>
#include <mach/hardware.h>

#define LPC4088_FLASHCFG	(LPC4088_SYSCTRL + 0x000)
# define LPC4088_FLASHTIM(x)	((((x) - 1) << 12) | 0x3a)
#define LPC4088_PLLCON(x)	(LPC4088_SYSCTRL + 0x080 + (x) * 0x20)
# define LPC4088_PLLE		(1 << 0)
#define LPC4088_PLLCFG(x)	(LPC4088_SYSCTRL + 0x084 + (x) * 0x20)
#define LPC4088_PLLSTAT(x)	(LPC4088_SYSCTRL + 0x088 + (x) * 0x20)
# define LPC4088_PLOCK		(1 << 10)
#define LPC4088_PLLFEED(x)	(LPC4088_SYSCTRL + 0x08c + (x) * 0x20)
#define LPC4088_PCON		(LPC4088_SYSCTRL + 0x0c0)
#define LPC4088_PCONP		(LPC4088_SYSCTRL + 0x0c4)
#define LPC4088_PCONP1		(LPC4088_SYSCTRL + 0x0c8)
#define LPC4088_EMCCLKSEL	(LPC4088_SYSCTRL + 0x100)
# define LPC4088_EMCDIV		(1 << 0)
#define LPC4088_CCLKSEL		(LPC4088_SYSCTRL + 0x104)
# define LPC4088_CCLKSRC	(1 << 8)
#define LPC4088_CLKSRCSEL	(LPC4088_SYSCTRL + 0x10c)
# define LPC4088_CLKSRC		(1 << 0)
#define LPC4088_SCS		(LPC4088_SYSCTRL + 0x1a0)
# define LPC4088_EMCSC		(1 << 0)
# define LPC4088_MCIPWRAL	(1 << 3)
# define LPC4088_OSCEN		(1 << 5)
# define LPC4088_OSCSTAT	(1 << 6)
#define LPC4088_PCLKSEL		(LPC4088_SYSCTRL + 0x1a8)
#define LPC4088_PBOOST		(LPC4088_SYSCTRL + 0x1b0)
# define LPC4088_BOOST		(3 << 0)
#define LPC4088_CLKOUTCFG	(LPC4088_SYSCTRL + 0x1c8)
#define LPC4088_EMCDLYCTL	(LPC4088_SYSCTRL + 0x1dc)
# define LPC4088_CMDDLY(x)	((x) << 0)
# define LPC4088_FBCLKDLY(x)	((x) << 8)
# define LPC4088_CLKOUT0DLY(x)	((x) << 16)

#define LPC4088_EMCCTRL		(LPC4088_EMC + 0x000)
# define LPC4088_EMCCTRL_E	(1 << 0)
#define LPC4088_EMCCONFIG	(LPC4088_EMC + 0x008)
#define LPC4088_DYNAMICCONTROL	(LPC4088_EMC + 0x020)
# define LPC4088_CE		(1 << 0)
# define LPC4088_CS		(1 << 1)
# define LPC4088_INIT(x)	((x) << 7)
# define LPC4088_DYCTRL_DEF	(LPC4088_CE | LPC4088_CS)
#define LPC4088_DYNAMICREFRESH	(LPC4088_EMC + 0x024)
#define LPC4088_DYNAMICREADCFG	(LPC4088_EMC + 0x028)
#define LPC4088_DYNAMICRP	(LPC4088_EMC + 0x030)
#define LPC4088_DYNAMICRAS	(LPC4088_EMC + 0x034)
#define LPC4088_DYNAMICSREX	(LPC4088_EMC + 0x038)
#define LPC4088_DYNAMICAPR	(LPC4088_EMC + 0x03c)
#define LPC4088_DYNAMICDAL	(LPC4088_EMC + 0x040)
#define LPC4088_DYNAMICWR	(LPC4088_EMC + 0x044)
#define LPC4088_DYNAMICRC	(LPC4088_EMC + 0x048)
#define LPC4088_DYNAMICRFC	(LPC4088_EMC + 0x04c)
#define LPC4088_DYNAMICXSR	(LPC4088_EMC + 0x050)
#define LPC4088_DYNAMICRRD	(LPC4088_EMC + 0x054)
#define LPC4088_DYNAMICMRD	(LPC4088_EMC + 0x058)
#define LPC4088_DYNAMICCFG(x)	(LPC4088_EMC + 0x100 + (x) * 0x20)
# define LPC4088_DYNAMICCFG_B	(1 << 19)
#define LPC4088_DYNAMICRASC(x)	(LPC4088_EMC + 0x104 + (x) * 0x20)

#define LPC4088_TIMER0_IR	(LPC4088_TIMER0 + 0x000)
# define LPC4088_MR0INT		(1 << 0)
#define LPC4088_TIMER0_TCR	(LPC4088_TIMER0 + 0x004)
# define LPC4088_CEN		(1 << 0)
# define LPC4088_CRST		(1 << 1)
#define LPC4088_TIMER0_PR	(LPC4088_TIMER0 + 0x00c)
#define LPC4088_TIMER0_MCR	(LPC4088_TIMER0 + 0x014)
# define LPC4088_MR0I		(1 << 0)
#define LPC4088_TIMER0_MR0	(LPC4088_TIMER0 + 0x018)
#define LPC4088_TIMER0_CTCR	(LPC4088_TIMER0 + 0x070)

/* Calculations against 60MHz of EMC clock */
#define EMC_FREQ		(60000000ULL)
#define EMC_NS2CLK(x)		(DIV_ROUND_UP((x) * EMC_FREQ, 1000000000ULL))
#define ZERO_BASED(x,m)		((x) ? min((u32)(x) - 1, (u32)(m)) : 0)
#define REFRESH(x,c)		(((x) * EMC_FREQ) / (16 * 1000 * (c)))

#if 0
/* DevBoard */
# define SDRAM_CFG		(0x0d)
# define REFRESH_CFG		REFRESH(64, 8192)
#else
# define SDRAM_CFG		(0xaa)
# define REFRESH_CFG		REFRESH(64, 4096)
#endif

#define PIN_FUNC(x)		((x) << 0)
#define PIN_PD			(1 << 3)
#define PIN_PU			(2 << 3)
#define PIN_HYS			(1 << 5)
#define PIN_INV			(1 << 6)
#define PIN_DIGITAL		(1 << 7)
#define PIN_NOISE		(1 << 8)
#define PIN_HS			PIN_NOISE
#define PIN_FAST		(1 << 9)
#define PIN_HIDRIVE		PIN_FAST
#define PIN_OD			(1 << 10)
#define PIN_DACEN		(1 << 16)

#define PIN_GPIO		(PIN_PU | PIN_HYS)

#define MPU_RASR_EN		(1 << 0)
#define MPU_RASR_SIZE_4GB	(31 << 1)
#define MPU_RASR_AP_RW_RW	(3 << 24)
#define MPU_CTRL_EN_MSK		(1 << 0)
#define MPU_CTRL_HFNMI_EN_MSK	(1 << 1)

static const u32 iocon[] = {
/* P0[0] */	PIN_FUNC(1) | PIN_PU,		/* CAN_RD1 */
/* P0[1] */	PIN_FUNC(1),			/* CAN_TD1 */
/* P0[2] */	PIN_FUNC(1),			/* U0_TXD */
/* P0[3] */	PIN_FUNC(1) | PIN_PU,		/* U0_RXD */
/* P0[4] */	PIN_FUNC(2) | PIN_PU,		/* CAN_RD2 */
/* P0[5] */	PIN_FUNC(2),			/* CAN_TD2 */
/* P0[6] */	PIN_FUNC(0) | PIN_GPIO,		/* GPIO */
/* P0[7] */	PIN_FUNC(1) | PIN_DIGITAL,	/* I2S_TX_SCK */
/* P0[8] */	PIN_FUNC(1) | PIN_DIGITAL,	/* I2S_TX_WS */
/* P0[9] */	PIN_FUNC(1) | PIN_DIGITAL,	/* I2S_TX_SDA */
/* P0[10] */	PIN_FUNC(1),			/* U2_TXD */
/* P0[11] */	PIN_FUNC(1) | PIN_PU,		/* U2_RXD */
/* P0[12] */	PIN_FUNC(3),			/* ADC[6] */
/* P0[13] */	PIN_FUNC(3),			/* ADC[7] */
/* P0[14] */	PIN_FUNC(0) | PIN_GPIO,		/* GPIO */
/* P0[15] */	PIN_FUNC(5) | PIN_FAST,		/* SPIFI_IO[2] */
/* P0[16] */	PIN_FUNC(5) | PIN_FAST,		/* SPIFI_IO[3] */
/* P0[17] */	PIN_FUNC(5) | PIN_FAST,		/* SPIFI_IO[1] */
/* P0[18] */	PIN_FUNC(5) | PIN_FAST,		/* SPIFI_IO[0] */
/* P0[19] */	PIN_FUNC(0) | PIN_GPIO,		/* GPIO */
/* P0[20] */	PIN_FUNC(1),			/* U1_DTR */
/* P0[21] */	PIN_FUNC(3),			/* U4_OE */
/* P0[22] */	PIN_FUNC(5) | PIN_FAST,		/* SPIFI_CLK */
/* P0[23] */	PIN_FUNC(1),			/* ADC[0] */
/* P0[24] */	PIN_FUNC(1),			/* ADC[1] */
/* P0[25] */	PIN_FUNC(1),			/* ADC[2] */
/* P0[26] */	PIN_FUNC(1),			/* ADC[3] */
/* P0[27] */	PIN_FUNC(1),			/* I2C0_SDA */
/* P0[28] */	PIN_FUNC(1),			/* I2C0_SCL */
/* P0[29] */	PIN_FUNC(1),			/* USB_D+1 */
/* P0[30] */	PIN_FUNC(1),			/* USB_D-1 */
/* P0[31] */	PIN_FUNC(0),			/* GPIO */
/* P1[0] */	PIN_FUNC(4),			/* SSP2_SCK */
/* P1[1] */	PIN_FUNC(4),			/* SSP2_MOSI */
/* P1[2] */	PIN_FUNC(2),			/* SD_CLK */
/* P1[3] */	PIN_FUNC(2),			/* SD_CMD */
/* P1[4] */	PIN_FUNC(4),			/* SSP2_MISO */
/* P1[5] */	PIN_FUNC(2) | PIN_DIGITAL,	/* SD_PWR */
/* P1[6] */	PIN_FUNC(2) | PIN_DIGITAL,	/* SD_DAT[0] */
/* P1[7] */	PIN_FUNC(2) | PIN_DIGITAL,	/* SD_DAT[1] */
/* P1[8] */	PIN_FUNC(0) | PIN_GPIO,		/* GPIO */
/* P1[9] */	PIN_FUNC(0) | PIN_GPIO,		/* GPIO */
/* P1[10] */	PIN_FUNC(0) | PIN_GPIO,		/* GPIO */
/* P1[11] */	PIN_FUNC(2),			/* SD_DAT[2] */
/* P1[12] */	PIN_FUNC(2),			/* SD_DAT[3] */
/* P1[13] */	PIN_FUNC(0) | PIN_GPIO,		/* GPIO */
/* P1[14] */	PIN_FUNC(0) | PIN_GPIO | PIN_DIGITAL, /* GPIO */
/* P1[15] */	PIN_FUNC(0) | PIN_GPIO,		/* GPIO */
/* P1[16] */	PIN_FUNC(2) | PIN_DIGITAL,	/* I2S_TX_MCLK */
/* P1[17] */	PIN_FUNC(0) | PIN_GPIO | PIN_DIGITAL, /* GPIO */
/* P1[18] */	PIN_FUNC(1),			/* USB_UP_LED1 */
/* P1[19] */	PIN_FUNC(2),			/* USB_PPWR1 */
/* P1[20] */	PIN_FUNC(0) | PIN_GPIO,		/* GPIO */
/* P1[21] */	PIN_FUNC(0) | PIN_GPIO,		/* GPIO */
/* P1[22] */	PIN_FUNC(0) | PIN_GPIO,		/* GPIO */
/* P1[23] */	PIN_FUNC(0) | PIN_GPIO,		/* GPIO */
/* P1[24] */	PIN_FUNC(0) | PIN_GPIO,		/* GPIO */
/* P1[25] */	PIN_FUNC(5),			/* CLKOUT */
/* P1[26] */	PIN_FUNC(0) | PIN_GPIO,		/* GPIO */
/* P1[27] */	PIN_FUNC(2),			/* USB_OVRCR1 */
/* P1[28] */	PIN_FUNC(0) | PIN_GPIO,		/* GPIO */
/* P1[29] */	PIN_FUNC(0) | PIN_GPIO,		/* GPIO */
/* P1[30] */	PIN_FUNC(3),			/* ADC[4] */
/* P1[31] */	PIN_FUNC(3),			/* ADC[5] */
/* P2[0] */	PIN_FUNC(2),			/* U1_TXD */
/* P2[1] */	PIN_FUNC(2) | PIN_PU,		/* U1_RXD */
/* P2[2] */	PIN_FUNC(0) | PIN_GPIO,		/* GPIO */
/* P2[3] */	PIN_FUNC(0) | PIN_GPIO,		/* GPIO */
/* P2[4] */	PIN_FUNC(0) | PIN_GPIO,		/* GPIO */
/* P2[5] */	PIN_FUNC(0) | PIN_GPIO,		/* GPIO */
/* P2[6] */	PIN_FUNC(0) | PIN_GPIO,		/* GPIO */
/* P2[7] */	PIN_FUNC(5) | PIN_FAST,		/* SPIFI_CS */
/* P2[8] */	PIN_FUNC(0) | PIN_GPIO,		/* GPIO */
/* P2[9] */	PIN_FUNC(0) | PIN_GPIO,		/* GPIO */
/* P2[10] */	PIN_FUNC(0) | PIN_GPIO,		/* GPIO */
/* P2[11] */	PIN_FUNC(0) | PIN_GPIO,		/* GPIO */
/* P2[12] */	PIN_FUNC(0) | PIN_GPIO,		/* GPIO */
/* P2[13] */	PIN_FUNC(0) | PIN_GPIO,		/* GPIO */
/* P2[14] */	PIN_FUNC(1) | PIN_FAST,		/* EMC_CS2 */
/* P2[15] */	PIN_FUNC(1) | PIN_FAST,		/* EMC_CS3 */
/* P2[16] */	PIN_FUNC(1) | PIN_FAST,		/* EMC_CAS */
/* P2[17] */	PIN_FUNC(1) | PIN_FAST,		/* EMC_RAS */
/* P2[18] */	PIN_FUNC(1) | PIN_FAST,		/* EMC_CLK0 */
/* P2[19] */	PIN_FUNC(0) | PIN_GPIO,		/* GPIO */
/* P2[20] */	PIN_FUNC(1) | PIN_FAST,		/* EMC_DYCS0 */
/* P2[21] */	PIN_FUNC(0) | PIN_GPIO,		/* GPIO */
/* P2[22] */	PIN_FUNC(2),			/* SSP0_SCK */
/* P2[23] */	PIN_FUNC(0) | PIN_GPIO,		/* GPIO */
/* P2[24] */	PIN_FUNC(1) | PIN_FAST,		/* EMC_CKE0 */
/* P2[25] */	PIN_FUNC(0) | PIN_GPIO,		/* GPIO */
/* P2[26] */	PIN_FUNC(2),			/* SSP0_MISO */
/* P2[27] */	PIN_FUNC(2),			/* SSP0_MOSI */
/* P2[28] */	PIN_FUNC(1) | PIN_FAST,		/* EMC_DQM0 */
/* P2[29] */	PIN_FUNC(1) | PIN_FAST,		/* EMC_DQM1 */
/* P2[30] */	PIN_FUNC(1) | PIN_FAST,		/* EMC_DQM2 */
/* P2[31] */	PIN_FUNC(1) | PIN_FAST,		/* EMC_DQM3 */
/* P3[0] */	PIN_FUNC(1) | PIN_FAST,		/* EMC_D[0] */
/* P3[1] */	PIN_FUNC(1) | PIN_FAST,		/* EMC_D[1] */
/* P3[2] */	PIN_FUNC(1) | PIN_FAST,		/* EMC_D[2] */
/* P3[3] */	PIN_FUNC(1) | PIN_FAST,		/* EMC_D[3] */
/* P3[4] */	PIN_FUNC(1) | PIN_FAST,		/* EMC_D[4] */
/* P3[5] */	PIN_FUNC(1) | PIN_FAST,		/* EMC_D[5] */
/* P3[6] */	PIN_FUNC(1) | PIN_FAST,		/* EMC_D[6] */
/* P3[7] */	PIN_FUNC(1) | PIN_FAST,		/* EMC_D[7] */
/* P3[8] */	PIN_FUNC(1) | PIN_FAST,		/* EMC_D[8] */
/* P3[9] */	PIN_FUNC(1) | PIN_FAST,		/* EMC_D[9] */
/* P3[10] */	PIN_FUNC(1) | PIN_FAST,		/* EMC_D[10] */
/* P3[11] */	PIN_FUNC(1) | PIN_FAST,		/* EMC_D[11] */
/* P3[12] */	PIN_FUNC(1) | PIN_FAST,		/* EMC_D[12] */
/* P3[13] */	PIN_FUNC(1) | PIN_FAST,		/* EMC_D[13] */
/* P3[14] */	PIN_FUNC(1) | PIN_FAST,		/* EMC_D[14] */
/* P3[15] */	PIN_FUNC(1) | PIN_FAST,		/* EMC_D[15] */
/* P3[16] */	PIN_FUNC(1) | PIN_FAST,		/* EMC_D[16] */
/* P3[17] */	PIN_FUNC(1) | PIN_FAST,		/* EMC_D[17] */
/* P3[18] */	PIN_FUNC(1) | PIN_FAST,		/* EMC_D[18] */
/* P3[19] */	PIN_FUNC(1) | PIN_FAST,		/* EMC_D[19] */
/* P3[20] */	PIN_FUNC(1) | PIN_FAST,		/* EMC_D[20] */
/* P3[21] */	PIN_FUNC(1) | PIN_FAST,		/* EMC_D[21] */
/* P3[22] */	PIN_FUNC(1) | PIN_FAST,		/* EMC_D[22] */
/* P3[23] */	PIN_FUNC(1) | PIN_FAST,		/* EMC_D[23] */
/* P3[24] */	PIN_FUNC(1) | PIN_FAST,		/* EMC_D[24] */
/* P3[25] */	PIN_FUNC(1) | PIN_FAST,		/* EMC_D[25] */
/* P3[26] */	PIN_FUNC(1) | PIN_FAST,		/* EMC_D[26] */
/* P3[27] */	PIN_FUNC(1) | PIN_FAST,		/* EMC_D[27] */
/* P3[28] */	PIN_FUNC(1) | PIN_FAST,		/* EMC_D[28] */
/* P3[29] */	PIN_FUNC(1) | PIN_FAST,		/* EMC_D[29] */
/* P3[30] */	PIN_FUNC(1) | PIN_FAST,		/* EMC_D[30] */
/* P3[31] */	PIN_FUNC(1) | PIN_FAST,		/* EMC_D[31] */
/* P4[0] */	PIN_FUNC(1) | PIN_FAST,		/* EMC_A[0] */
/* P4[1] */	PIN_FUNC(1) | PIN_FAST,		/* EMC_A[1] */
/* P4[2] */	PIN_FUNC(1) | PIN_FAST,		/* EMC_A[2] */
/* P4[3] */	PIN_FUNC(1) | PIN_FAST,		/* EMC_A[3] */
/* P4[4] */	PIN_FUNC(1) | PIN_FAST,		/* EMC_A[4] */
/* P4[5] */	PIN_FUNC(1) | PIN_FAST,		/* EMC_A[5] */
/* P4[6] */	PIN_FUNC(1) | PIN_FAST,		/* EMC_A[6] */
/* P4[7] */	PIN_FUNC(1) | PIN_FAST,		/* EMC_A[7] */
/* P4[8] */	PIN_FUNC(1) | PIN_FAST,		/* EMC_A[8] */
/* P4[9] */	PIN_FUNC(1) | PIN_FAST,		/* EMC_A[9] */
/* P4[10] */	PIN_FUNC(1) | PIN_FAST,		/* EMC_A[10] */
/* P4[11] */	PIN_FUNC(1) | PIN_FAST,		/* EMC_A[11] */
/* P4[12] */	PIN_FUNC(1) | PIN_FAST,		/* EMC_A[12] */
/* P4[13] */	PIN_FUNC(1) | PIN_FAST,		/* EMC_A[13] */
/* P4[14] */	PIN_FUNC(1) | PIN_FAST,		/* EMC_A[14] */
/* P4[15] */	PIN_FUNC(0) | PIN_GPIO,		/* GPIO */
/* P4[16] */	PIN_FUNC(0) | PIN_GPIO,		/* GPIO */
/* P4[17] */	PIN_FUNC(0) | PIN_GPIO,		/* GPIO */
/* P4[18] */	PIN_FUNC(0) | PIN_GPIO,		/* GPIO */
/* P4[19] */	PIN_FUNC(0) | PIN_GPIO,		/* GPIO */
/* P4[20] */	PIN_FUNC(3),			/* SSP1_SCK */
/* P4[21] */	PIN_FUNC(0) | PIN_GPIO,		/* GPIO */
/* P4[22] */	PIN_FUNC(3),			/* SSP1_MISO */
/* P4[23] */	PIN_FUNC(3),			/* SSP1_MOSI */
/* P4[24] */	PIN_FUNC(1) | PIN_FAST,		/* EMC_OE */
/* P4[25] */	PIN_FUNC(1) | PIN_FAST,		/* EMC_WE */
/* P4[26] */	PIN_FUNC(0) | PIN_GPIO,		/* GPIO */
/* P4[27] */	PIN_FUNC(0) | PIN_GPIO,		/* GPIO */
/* P4[28] */	PIN_FUNC(2),			/* U3_TXD */
/* P4[29] */	PIN_FUNC(2) | PIN_PU,		/* U3_RXD */
/* P4[30] */	PIN_FUNC(1) | PIN_FAST,		/* EMC_CS0 */
/* P4[31] */	PIN_FUNC(1) | PIN_FAST,		/* EMC_CS1 */
/* P5[0] */	PIN_FUNC(0) | PIN_GPIO,		/* GPIO */
/* P5[1] */	PIN_FUNC(0) | PIN_GPIO,		/* GPIO */
/* P5[2] */	PIN_FUNC(0) | PIN_GPIO,		/* GPIO */
/* P5[3] */	PIN_FUNC(4),			/* U4 RXD */
/* P5[4] */	PIN_FUNC(4),			/* U4_TXD */
};

static inline void nop(void)
{
	__asm__ __volatile__("nop\n");
}

static void writepll(const u32 idx, const u32 cfg)
{
	writel(cfg - 1, LPC4088_PLLCFG(idx));
	writel(LPC4088_PLLE, LPC4088_PLLCON(idx));
	writel(0xaa, LPC4088_PLLFEED(idx));
	writel(0x55, LPC4088_PLLFEED(idx));
	do {
		nop();
	} while (!(readl(LPC4088_PLLSTAT(idx)) & LPC4088_PLOCK));
}

static void wait_us(const u32 us)
{
	/* Timer mode */
	writel(0, LPC4088_TIMER0_CTCR);
	/* Prescaler = 60, Timer tick is 1 MHz */
	writel(60 - 1, LPC4088_TIMER0_PR);
	/* Reset prescaler counter and timer counter */
	writel(LPC4088_CRST, LPC4088_TIMER0_TCR);
	/* Match value */
	writel(us, LPC4088_TIMER0_MR0);
	/* Clear interrupt flags */
	writel(~0, LPC4088_TIMER0_IR);
	/* Release reset, enable timer */
	writel(LPC4088_MR0I, LPC4088_TIMER0_MCR);
	/* Enable match interrupt */
	writel(LPC4088_CEN, LPC4088_TIMER0_TCR);
	/* Wait for compare match */
	do {
	} while (!(readl(LPC4088_TIMER0_IR) & LPC4088_MR0INT));
}

void __naked barebox_arm_reset_vector(void)
{
	unsigned i;

	/* Configure the memory protection unit (MPU) to allow full access to */
	/* the whole 4GB address space. */
	writel(0, LPC4088_MPU_RNR);
	writel(0, LPC4088_MPU_RBAR);
	writel(MPU_RASR_AP_RW_RW | MPU_RASR_SIZE_4GB | MPU_RASR_EN,
	       LPC4088_MPU_RASR);
	writel(MPU_CTRL_EN_MSK | MPU_CTRL_HFNMI_EN_MSK, LPC4088_MPU_CTRL);

	/* Turn ON main oscillator, */
	writel(LPC4088_EMCSC | LPC4088_OSCEN | LPC4088_MCIPWRAL, LPC4088_SCS);
	do {
		nop();
	} while (!(readl(LPC4088_SCS) & LPC4088_OSCSTAT));

	/* Selects the main oscillator as the SYSCLK and PLL0 source */
	writel(LPC4088_CLKSRC, LPC4088_CLKSRCSEL);

	/* Mul = 10, PLL0 out = 120 MHz */
	writepll(0, 10);
	/* Mul = 12, PLL1 out = 144 MHz */
	writepll(1, 12);

	/* PLL0 is used as CPU clock, Divider = 1 */
	writel(LPC4088_CCLKSRC | 1, LPC4088_CCLKSEL);

	/* EMC uses half rate of the CPU clock */
	writel(LPC4088_EMCDIV, LPC4088_EMCCLKSEL);

	/* APB clock divider = 2 */
	writel(2, LPC4088_PCLKSEL);

	/* Power Mode Control */
	writel(0, LPC4088_PCON);
	/* Power Control for Peripherals 0 */
	writel(0xb4218a9a, LPC4088_PCONP);
	/* Power Control for Peripherals 1 */
	writel(0x00000000, LPC4088_PCONP1);
	/* Power Boost Control */
	writel(LPC4088_BOOST, LPC4088_PBOOST);

	/* Disable CLKOUT pin */
	writel(0, LPC4088_CLKOUTCFG);

	/* Flash accesses use 5 CPU clocks */
	writel(LPC4088_FLASHTIM(5), LPC4088_FLASHCFG);

	/* Pin configuration */
	for (i = 0; i < ARRAY_SIZE(iocon); i++)
		writel(iocon[i], LPC4088_PINCTRL + i * 4);

	if (IS_ENABLED(CONFIG_DEBUG_LL)) {
		lpc408x_uart_lowlevel_init();
		puts_ll("LPC40xx started\n");
	}

	/* EMC delay control */
	writel(LPC4088_CMDDLY(8) | LPC4088_FBCLKDLY(8) | LPC4088_CLKOUT0DLY(8),
	       LPC4088_EMCDLYCTL);
	/* EMC enable */
	writel(LPC4088_EMCCTRL_E, LPC4088_EMCCTRL);
	/* Little-endian mode */
	writel(0, LPC4088_EMCCONFIG);
	/* Use EMCCLKDELAY */
	writel(1, LPC4088_DYNAMICREADCFG);
	/* RAS, CAS latency = 3 */
	writel((3 << 8) | (3 << 0), LPC4088_DYNAMICRASC(0));
	/* Precharge command period */
	writel(ZERO_BASED(EMC_NS2CLK(18), 0x0f), LPC4088_DYNAMICRP);
	/* Active to precharge command period */
	writel(ZERO_BASED(EMC_NS2CLK(42), 0x0f), LPC4088_DYNAMICRAS);
	/* Self-refresh exit time */
	writel(ZERO_BASED(EMC_NS2CLK(67), 0x0f), LPC4088_DYNAMICSREX);
	/* Last-data-out to active command time */
	writel(1, LPC4088_DYNAMICAPR);
	/* Data-in to active command time */
	writel(5 - 1, LPC4088_DYNAMICDAL);
	/* Write recovery time */
	writel(3 - 1, LPC4088_DYNAMICWR);
	/* Active to active command period */
	writel(ZERO_BASED(EMC_NS2CLK(60), 0x1f), LPC4088_DYNAMICRC);
	/* Auto-refresh period */
	writel(ZERO_BASED(EMC_NS2CLK(60), 0x1f), LPC4088_DYNAMICRFC);
	/* Time for exit self-refresh to active command */
	writel(ZERO_BASED(EMC_NS2CLK(67), 0x1f), LPC4088_DYNAMICXSR);
	/* Latency for active bank A to active bank B */
	writel(ZERO_BASED(EMC_NS2CLK(12), 0x0f), LPC4088_DYNAMICRRD);
	/* Time for load mode register to active command */
	writel(2 - 1, LPC4088_DYNAMICMRD);

	/* Configuration information for EMC_DYCS0 */
	writel(SDRAM_CFG << 7, LPC4088_DYNAMICCFG(0));

	/* Issue SDRAM NOP */
	writel(LPC4088_DYCTRL_DEF | LPC4088_INIT(3), LPC4088_DYNAMICCONTROL);
	/* Wait 100 us */
	wait_us(100);
	/* Issue SDRAM PALL */
	writel(LPC4088_DYCTRL_DEF | LPC4088_INIT(2), LPC4088_DYNAMICCONTROL);
	/* Refresh timer */
	writel(2, LPC4088_DYNAMICREFRESH);
	for (i = 256; i; --i) {}
	writel(REFRESH_CFG, LPC4088_DYNAMICREFRESH);
	/* Issue SDRAM MODE */
	writel(LPC4088_DYCTRL_DEF | LPC4088_INIT(1), LPC4088_DYNAMICCONTROL);
	/* Mode Register Setting */
	readl(LPC4088_DRAM_CS0 | (0x33 << (2 + 8 + 2)));
	/* Issue SDRAM NORMAL */
	writel(LPC4088_INIT(0), LPC4088_DYNAMICCONTROL);
	/* Enable buffers */
	writel(readl(LPC4088_DYNAMICCFG(0)) | LPC4088_DYNAMICCFG_B,
	       LPC4088_DYNAMICCFG(0));
	for (i = 100000; i; --i) {}

	barebox_arm_entry(LPC4088_DRAM_CS0, SZ_16M, NULL);
}
