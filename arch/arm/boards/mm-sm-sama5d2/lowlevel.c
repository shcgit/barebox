/* SPDX-License-Identifier: GPL-2.0+ */
/* SPDX-FileCopyrightText: Alexander Shiyan <shc_work@mail.ru> */

#include <common.h>
#include <init.h>
#include <mach/at91/barebox-arm.h>
#include <mach/at91/sama5d2_ll.h>
#include <mach/at91/xload.h>
#include <debug_ll.h>
#include <mach/at91/sama5d2-sip-ddramc.h>

/* PCK = 492MHz, MCK = 82MHz */
#define MASTER_CLOCK	82000000

SAMA5D2_ENTRY_FUNCTION(start_mm_sm_sama5d2_xload_mmc, r4)
{
	sama5d2_lowlevel_init();

	pbl_set_putc(at91_dbgu_putc, sama5d2_resetup_uart_console(MASTER_CLOCK));

	putc_ll('>');

	relocate_to_current_adr();
	setup_c();

	sama5d2_udelay_init(MASTER_CLOCK);

	sama5d2_d1g_ddrconf();

	sama5d2_sdhci_start_image(r4);
}

SAMA5D2_ENTRY_FUNCTION(start_mm_sm_sama5d2, r4)
{
	extern char __dtb_z_mm_sm_sama5d2_base_start[];
	void *fdt = __dtb_z_mm_sm_sama5d2_base_start + get_runtime_offset();

	putc_ll('>');

	sama5d2_barebox_entry(r4, fdt);
}
