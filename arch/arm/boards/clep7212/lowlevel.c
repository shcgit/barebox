/*
 * Copyright (C) 2012 Alexander Shiyan <shc_work@mail.ru>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 */

#include <common.h>
#include <init.h>

#include <asm/barebox-arm.h>
#include <asm/barebox-arm-head.h>

#include <mach/clps711x.h>

#ifdef CONFIG_CLPS711X_RAISE_CPUFREQ
# define CLPS711X_CPU_PLL_MULT	50
#else
# define CLPS711X_CPU_PLL_MULT	40
#endif

void __naked __bare_init barebox_arm_reset_vector(void)
{
	extern char __dtb_clps711x_start[];
	void *fdt;

	arm_cpu_lowlevel_init();

	/* Stack in SRAM */
	arm_setup_stack(CS6_BASE - 16);

	fdt = __dtb_clps711x_start + get_runtime_offset();

	clps711x_barebox_entry(CLPS711X_CPU_PLL_MULT, fdt);
}
