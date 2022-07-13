/* SPDX-License-Identifier: GPL-2.0+ */
/* SPDX-FileCopyrightText: Alexander Shiyan <shc_work@mail.ru> */

#include <bootsource.h>
#include <common.h>
#include <driver.h>
#include <envfs.h>
#include <init.h>
#include <linux/sizes.h>
#include <mach/am33xx-generic.h>

#ifdef CONFIG_I2C

#include <i2c/i2c.h>

static void myir_set_timing(struct device_node *root, const char *timingpath)
{
	struct device_node *display = of_find_node_by_path_from(root, timingpath);
	if (display)
		of_set_property_to_child_phandle(display, "native-mode");
}

static void myir_disable_device(struct device_node *root, const char *label)
{
	struct device_node *np = of_find_node_by_name(root, label);
	if (np)
		of_device_disable(np);
}

static int myir_probe_i2c(struct i2c_adapter *adapter, int addr, u8 cmd)
{
	u8 buf[1] = { cmd, };
	struct i2c_msg msg = {
		.addr = addr,
		.buf = buf,
		.len = sizeof(buf),
		.flags = I2C_M_RD,
	};

	return (i2c_transfer(adapter, &msg, 1) == 1) ? buf[0] : -ENODEV;
}

#define SGTL5000_ADDR	0x0a
#define AIC3100_ADDR	0x18
#define ISL97671_ADDR	0x2c
#define PCA9536_ADDR	0x41

static int myir_board_fixup(struct device_node *root, void *unused)
{
	int dispver;
	struct i2c_adapter *adapter = i2c_get_adapter(0);
	if (!adapter)
		return -ENODEV;

	if (myir_probe_i2c(adapter, SGTL5000_ADDR, 0) < 0)
		myir_disable_device(root, "sound");
	else if (myir_probe_i2c(adapter, AIC3100_ADDR, 0) < 0)
		myir_disable_device(root, "sound1");

	if (myir_probe_i2c(adapter, ISL97671_ADDR, 0) >= 0) {
		myir_set_timing(root, "/panel/display-timings/PH320240T");
		return 0;
	}

	dispver = myir_probe_i2c(adapter, PCA9536_ADDR, 0);
	if (dispver < 0) {
		pr_warn("Display not detected!\n");
		return 0;
	}

	dispver &= 0x0f;

	switch (dispver) {
	case 0:
		/* G104XVN01 */
		myir_set_timing(root, "/panel/display-timings/G104XVN01");
		break;
//	case 1:
//		/* AT070TN94 */
//		myir_set_timing(root, "/panel/display-timings/AT070TN94");
//		break;
	default:
		pr_warn("Unhandled display version: %i.\n", dispver);
		break;
	}

	return 0;
}

static __init int myir_i2c_fixup(void)
{
	if (of_machine_is_compatible("milas,informer-am335x") ||
	    of_machine_is_compatible("milas,spider-am335x"))
		of_register_fixup(myir_board_fixup, NULL);

	return 0;
}
postcore_initcall(myir_i2c_fixup);

#endif /* CONFIG_I2C */

static struct omap_barebox_part myir_barebox_part = {
	.nand_offset = SZ_128K * 4,
	.nand_size = SZ_1M,
};

static __init int myir_devices_init(void)
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

	if (IS_ENABLED(CONFIG_DEFAULT_ENVIRONMENT))
		defaultenv_append_directory(defaultenv_myirtech_x335x);

	if (IS_ENABLED(CONFIG_SHELL_NONE))
		return am33xx_of_register_bootdevice();

	return 0;
}
coredevice_initcall(myir_devices_init);
