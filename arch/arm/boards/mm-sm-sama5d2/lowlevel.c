// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2019 Ahmad Fatoum, Pengutronix
 */

#include <common.h>
#include <init.h>
#include <mach/at91/barebox-arm.h>
#include <mach/at91/sama5d2_ll.h>
//#include <mach/at91/iomux.h>
#include <mach/at91/xload.h>
#include <debug_ll.h>
#include <mach/at91/sama5d2-sip-ddramc.h>

#define RGB_LED_GREEN (1 << 0)
#define RGB_LED_RED   (1 << 1)
#define RGB_LED_BLUE  (1 << 2)

/* PCK = 492MHz, MCK = 164MHz */
#define MASTER_CLOCK	164000000

SAMA5D2_ENTRY_FUNCTION(start_mm_sm_sama5d2_xload_mmc, r4)
{
	void __iomem *dbgu_base;
	sama5d2_lowlevel_init();

	dbgu_base = sama5d2_resetup_uart_console(MASTER_CLOCK);
	putc_ll('>');

	relocate_to_current_adr();
	setup_c();

	pbl_set_putc(at91_dbgu_putc, dbgu_base);

	sama5d2_udelay_init(MASTER_CLOCK);
	sama5d2_d1g_ddrconf();
	sama5d2_sdhci_start_image(r4);
}

SAMA5D2_ENTRY_FUNCTION(start_mm_sm_sama5d2, r4)
{
	extern char __dtb_z_mm_sm_sama5d2_evb_start[];
	void *fdt = __dtb_z_mm_sm_sama5d2_evb_start + get_runtime_offset();

	putc_ll('>');

	sama5d2_barebox_entry(r4, fdt);
}
