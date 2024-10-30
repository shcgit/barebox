// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2019 Rouven Czerwinski, Pengutronix
 */
#define pr_fmt(fmt) "tqma6ul: " fmt

#include <common.h>
#include <debug_ll.h>
#include <mach/imx/debug_ll.h>
#include <firmware.h>
#include <mach/imx/generic.h>
#include <asm/barebox-arm.h>
#include <mach/imx/esdctl.h>
#include <mach/imx/iomux-mx6ul.h>
#include <asm/cache.h>
#include <pbl/i2c.h>
#include <boards/tq/tq_eeprom.h>
#include <tee/optee.h>

#include "tqma6ulx.h"

extern char __dtb_z_imx6ul_tqma6ul2_mba6ulx_start[];
extern char __dtb_z_imx6ul_tqma6ul2l_mba6ulx_start[];
extern char __dtb_z_imx6ull_tqma6ull2_mba6ulx_start[];
extern char __dtb_z_imx6ull_tqma6ull2l_mba6ulx_start[];

static void setup_uart(void)
{
	imx6_ungate_all_peripherals();

	/*
	 * Default pad configuration on this board, no explicit config needed
	 */
	imx6_uart_setup((void *)MX6_UART1_BASE_ADDR);
	pbl_set_putc(imx_uart_putc, (void *)MX6_UART1_BASE_ADDR);

	pr_debug("\n");

}

static void *read_eeprom(void)
{
	struct pbl_i2c *i2c;
	struct tq_eeprom *eeprom;
	void __iomem *iomux = (void *)MX6_IOMUXC_BASE_ADDR;
	void *fdt = __dtb_z_imx6ul_tqma6ul2l_mba6ulx_start;

	imx_setup_pad(iomux, MX6_PAD_UART2_TX_DATA__I2C4_SCL | MUX_PAD_CTRL(0x1b8b0));
	imx_setup_pad(iomux, MX6_PAD_UART2_RX_DATA__I2C4_SDA | MUX_PAD_CTRL(0x1b8b0));

	i2c = imx6_i2c_early_init(IOMEM(MX6_I2C4_BASE_ADDR));

	eeprom = pbl_tq_read_eeprom(i2c, 0x50, I2C_ADDR_16_BIT);
	if (!eeprom) {
		pr_err("Cannot read EEPROM\n");
		goto out;
	}

	pr_info("Board: %s\n", eeprom->id);

	if (!strcmp(eeprom->id, "TQMa6UL2L-AB.0202"))
		fdt = __dtb_z_imx6ul_tqma6ul2l_mba6ulx_start;
	else
		pr_err("Unknown board type\n");
out:
	return fdt;
}

static void noinline start_mba6ulx(u32 r0)
{
	void *fdt;
	int tee_size;
	void *tee;

	setup_uart();

	fdt = read_eeprom();

	/* Enable normal/secure r/w for TZC380 region0 */
	writel(0xf0000000, 0x021D0108);

	/*
	 * Chainloading barebox will pass a device tree within the RAM in r0,
	 * skip OP-TEE early loading in this case
	 */
	if (IS_ENABLED(CONFIG_FIRMWARE_TQMA6UL_OPTEE) &&
	    !(r0 > MX6_MMDC_P0_BASE_ADDR &&
	      r0 < MX6_MMDC_P0_BASE_ADDR + SZ_256M)) {
		get_builtin_firmware(mba6ul_optee_bin, &tee, &tee_size);

		memset((void *)OPTEE_OVERLAY_LOCATION, 0, 0x1000);

		start_optee_early(NULL, tee);
	}

	imx6ul_barebox_entry(fdt);
}

ENTRY_FUNCTION(start_imx6ul_mba6ulx, r0, r1, r2)
{

	imx6ul_cpu_lowlevel_init();

	arm_setup_stack(0x00910000);

	if (IS_ENABLED(CONFIG_DEBUG_LL)) {
		imx6_uart_setup_ll();
		putc_ll('>');
	}

	relocate_to_current_adr();
	setup_c();
	barrier();

	start_mba6ulx(r0);
}
