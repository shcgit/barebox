// SPDX-License-Identifier: GPL-2.0+
// Author: Alexander Shiyan <shc_work@mail.ru>

#include <common.h>
#include <init.h>
#include <linux/sizes.h>

#include <asm/io.h>
#include <asm/barebox-arm.h>
#include <asm/barebox-arm-head.h>

#include "clps711x.h"

#ifdef CONFIG_CLPS711X_RAISE_CPUFREQ
# define CLPS711X_CPU_PLL_MULT	50
#else
# define CLPS711X_CPU_PLL_MULT	40
#endif

#ifdef CONFIG_MACH_CLPS711X_CLEP7212
# define __dtb_ptr_start	__dtb_ep7211_edb7211_start
#endif

void __naked __bare_init barebox_arm_reset_vector(void)
{
	extern char *__dtb_ptr_start[];
	void *fdt = __dtb_ptr_start;
	u32 bus;

	arm_cpu_lowlevel_init();

	/* Stack in SRAM */
	arm_setup_stack(CS6_BASE - 16);

	/* Check if we running from external 13 MHz clock */
	if (!(readl(SYSFLG2) & SYSFLG2_CKMODE)) {
		/* Setup bus wait state scaling factor to 2  */
		writel(SYSCON3_CLKCTL0 | SYSCON3_CLKCTL1, SYSCON3);
		asm("nop");

		/* Setup PLL */
		writel(CLPS711X_CPU_PLL_MULT << 24, PLLW);
		asm("nop");

		/* Check for old CPUs without PLL */
		if ((readl(PLLR) >> 24) != CLPS711X_CPU_PLL_MULT)
			bus = 73728000 / 2;
		else
			bus = CLPS711X_CPU_PLL_MULT * 3686400 / 2;
	} else {
		bus = 13000000;
		/* Setup bus wait state scaling factor to 1  */
		writel(0, SYSCON3);
		asm("nop");
	}

	/* CLKEN select, SDRAM width=32 */
	writel(SYSCON2_CLKENSL, SYSCON2);

	/* Setup SDRAM params (64MB, 16Bit*2, CAS=3) */
	writel(SDCONF_CASLAT_3 | SDCONF_SIZE_256 | SDCONF_WIDTH_16 |
	       SDCONF_CLKCTL | SDCONF_ACTIVE, SDCONF);

	/* Setup Refresh Rate (64ms 8K Blocks) */
	writel((64 * bus) / (8192 * 1000), SDRFPR);

	/* Disable UART, IrDa, LCD */
	writel(0, SYSCON1);
	/* Disable PWM */
	writew(0, PMPCON);
	/* Disable LED flasher */
	writew(0, LEDFLSH);

	barebox_arm_entry(SDRAM0_BASE, SZ_8M, fdt + get_runtime_offset());
}
