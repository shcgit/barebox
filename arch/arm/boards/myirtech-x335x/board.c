// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2008 Raghavendra KH <r-khandenahally@ti.com>, Texas Instruments (http://www.ti.com/)
// SPDX-FileCopyrightText: 2012 Jan Luebbe <j.luebbe@pengutronix.de>

/**
 * @file
 * @brief BeagleBone Specific Board Initialization routines
 */

#include <common.h>
#include <init.h>
#include <driver.h>
#include <envfs.h>
#include <environment.h>
#include <globalvar.h>
#include <linux/sizes.h>
#include <net.h>
#include <bootsource.h>
#include <asm/armlinux.h>
#include <generated/mach-types.h>
#include <mach/am33xx-silicon.h>
#include <mach/sys_info.h>
#include <mach/syslib.h>
#include <mach/gpmc.h>
#include <linux/err.h>
#include <mach/am33xx-generic.h>

static struct omap_barebox_part myir_barebox_part = {
	.nand_offset = SZ_128K,
	.nand_size = SZ_512K,
};

static __init int beaglebone_devices_init(void)
{
	if (!of_machine_is_compatible("myir,myc-am335x"))
		return 0;

	am33xx_register_ethaddr(0, 0);
	am33xx_register_ethaddr(1, 1);

	switch (bootsource_get()) {
	case BOOTSOURCE_MMC:
		omap_set_bootmmc_devname("mmc0");
		break;
	case BOOTSOURCE_NAND:
		omap_set_barebox_part(&myir_barebox_part);
		break;
	default:
		break;
	}

	defaultenv_append_directory(defaultenv_beaglebone);

	if (IS_ENABLED(CONFIG_SHELL_NONE))
		return am33xx_of_register_bootdevice();

	return 0;
}
coredevice_initcall(beaglebone_devices_init);
