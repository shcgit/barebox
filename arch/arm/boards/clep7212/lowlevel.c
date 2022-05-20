// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: Alexander Shiyan <shc_work@mail.ru>

#include <asm/barebox-arm.h>
#include <common.h>
#include <mach/clps711x.h>

extern char __dtb_ep7212_clep7212_start[];

ENTRY_FUNCTION(start_ep7212_clep7212, r0, r1, r2)
{
	void *fdt;

	arm_cpu_lowlevel_init();

	/* Stack in SRAM */
	arm_setup_stack(CS6_BASE - 16);

	fdt = __dtb_ep7212_clep7212_start;

	clps711x_start(fdt + get_runtime_offset());
}
