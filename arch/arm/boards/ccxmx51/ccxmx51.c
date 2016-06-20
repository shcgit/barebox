/*
 * (C) Copyright 2009-2010 Digi International, Inc.
 * Copyright (C) 2007 Sascha Hauer, Pengutronix
 * (c) 2011 Eukrea Electromatique, Eric Bénard <eric@eukrea.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <common.h>
#include <envfs.h>
#include <init.h>
#include <net.h>
#include <notifier.h>
#include <asm/armlinux.h>
#include <linux/sizes.h>
#include <mach/esdctl.h>
#include <mach/iim.h>
#include <mach/imx5.h>
#include <mach/imx51-regs.h>
#include <mach/revision.h>
#include <mfd/mc13xxx.h>
#include <mfd/mc13892.h>

static const struct ccxmx51_ident {
	char		*id_string;
	unsigned int	mem_sz;
	unsigned char	industrial;
	unsigned char	eth0:1;
	unsigned char	eth1:1;
	unsigned char	wless:1;
	unsigned char	accel:1;
} *ccxmx51_id, ccxmx51_ids[] = {
/* 0x00 */	{ "Unknown",						0,       0, 0, 0, 0, 0 },
/* 0x01 */	{ "Not supported",					0,       0, 0, 0, 0, 0 },
/* 0x02 */	{ "i.MX515@800MHz, Wireless, PHY, Ext. Eth, Accel",	SZ_512M, 0, 1, 1, 1, 1 },
/* 0x03 */	{ "i.MX515@800MHz, PHY, Ext. Eth, Accel",		SZ_512M, 0, 1, 1, 0, 1 },
/* 0x04 */	{ "i.MX515@600MHz, Wireless, PHY, Ext. Eth, Accel",	SZ_512M, 1, 1, 1, 1, 1 },
/* 0x05 */	{ "i.MX515@600MHz, PHY, Ext. Eth, Accel",		SZ_512M, 1, 1, 1, 0, 1 },
/* 0x06 */	{ "i.MX515@800MHz, Wireless, PHY, Accel",		SZ_512M, 0, 1, 0, 1, 1 },
/* 0x07 */	{ "i.MX515@800MHz, PHY, Accel",				SZ_512M, 0, 1, 0, 0, 1 },
/* 0x08 */	{ "i.MX515@800MHz, Wireless, PHY, Accel",		SZ_256M, 0, 1, 0, 1, 1 },
/* 0x09 */	{ "i.MX515@800MHz, PHY, Accel",				SZ_256M, 0, 1, 0, 0, 1 },
/* 0x0a */	{ "i.MX515@600MHz, Wireless, PHY, Accel",		SZ_256M, 1, 1, 0, 1, 1 },
/* 0x0b */	{ "i.MX515@600MHz, PHY, Accel",				SZ_256M, 1, 1, 0, 0, 1 },
/* 0x0c */	{ "i.MX515@800MHz, Wireless, PHY, Accel",		SZ_128M, 0, 1, 0, 1, 1 },
/* 0x0d */	{ "i.MX512@800MHz",					SZ_128M, 0, 0, 0, 0, 0 },
/* 0x0e */	{ "i.MX515@800MHz, Wireless, PHY, Accel",		SZ_512M, 0, 1, 0, 1, 1 },
/* 0x0f */	{ "i.MX515@600MHz, PHY, Accel",				SZ_128M, 1, 1, 0, 0, 1 },
/* 0x10 */	{ "i.MX515@600MHz, Wireless, PHY, Accel",		SZ_128M, 1, 1, 0, 1, 1 },
/* 0x11 */	{ "i.MX515@800MHz, PHY, Accel",				SZ_128M, 0, 1, 0, 0, 1 },
/* 0x12 */	{ "i.MX515@600MHz, Wireless, PHY, Accel",		SZ_512M, 1, 1, 0, 1, 1 },
/* 0x13 */	{ "i.MX515@800MHz, PHY, Accel",				SZ_512M, 0, 1, 0, 0, 1 },
};

static u32 boardserial;

static void ccxmx51_power_init(struct mc13xxx *mc13xxx)
{
	u32 val;

	mc13xxx_reg_read(mc13xxx, MC13892_REG_POWER_MISC, &val);
	/* Reset devices by clearing GP01-GPO4 */
	val &= ~((1 << 21) | (3 << 12) | (3 << 10) | (3 << 8) | (3 << 6));
	/* Switching off the PWGT1SPIEN */
	val |= (1 << 15);
	/* Switching on the PWGT2SPIEN */
	val &= ~(1 << 16);
	/* Enable short circuit protection */
	val |= (1 << 0);
	mc13xxx_reg_write(mc13xxx, MC13892_REG_POWER_MISC, val);

	/* Allow charger to charge (4.2V and 560mA) */
	val = 0x238033;
	mc13xxx_reg_write(mc13xxx, MC13892_REG_CHARGE, val);

	if (imx_silicon_revision() < IMX_CHIP_REV_3_0) {
		/* Set core voltage (SW1) to 1.1V */
		mc13xxx_reg_read(mc13xxx, MC13892_REG_SW_0, &val);
		val &= ~0x00001f;
		val |=  0x000014;
		mc13xxx_reg_write(mc13xxx, MC13892_REG_SW_0, val);

		/* Setup VCC (SW2) to 1.25 */
		mc13xxx_reg_read(mc13xxx, MC13892_REG_SW_1, &val);
		val &= ~0x00001f;
		val |=  0x00001a;
		mc13xxx_reg_write(mc13xxx, MC13892_REG_SW_1, val);

		/* Setup 1V2_DIG1 (SW3) to 1.25 */
		mc13xxx_reg_read(mc13xxx, MC13892_REG_SW_2, &val);
		val &= ~0x00001f;
		val |=  0x00001a;
		mc13xxx_reg_write(mc13xxx, MC13892_REG_SW_2, val);
	} else {
		/* Setup VCC (SW2) to 1.225 */
		mc13xxx_reg_read(mc13xxx, MC13892_REG_SW_1, &val);
		val &= ~0x00001f;
		val |=  0x000019;
		mc13xxx_reg_write(mc13xxx, MC13892_REG_SW_1, val);

		/* Setup 1V2_DIG1 (SW3) to 1.2 */
		mc13xxx_reg_read(mc13xxx, MC13892_REG_SW_2, &val);
		val &= ~0x00001f;
		val |=  0x000018;
		mc13xxx_reg_write(mc13xxx, MC13892_REG_SW_2, val);
	}

	if (mc13xxx_revision(mc13xxx) <= MC13892_REVISION_2_0) {
		/* Set switchers in PWM mode for Atlas 2.0 and lower */
		/* Setup the switcher mode for SW1 & SW2*/
		mc13xxx_reg_read(mc13xxx, MC13892_REG_SW_4, &val);
		val &= ~(MC13892_SWMODE_MASK << MC13892_SWMODE1_SHIFT);
		val |=  MC13892_SWMODE_PWM_PWM << MC13892_SWMODE1_SHIFT;
		val &= ~(MC13892_SWMODE_MASK << MC13892_SWMODE2_SHIFT);
		val |=  MC13892_SWMODE_PWM_PWM << MC13892_SWMODE2_SHIFT;
		mc13xxx_reg_write(mc13xxx, MC13892_REG_SW_4, val);

		/* Setup the switcher mode for SW3 & SW4 */
		mc13xxx_reg_read(mc13xxx, MC13892_REG_SW_5, &val);
		val &= ~(MC13892_SWMODE_MASK << MC13892_SWMODE3_SHIFT);
		val |=  MC13892_SWMODE_PWM_PWM << MC13892_SWMODE3_SHIFT;
		val &= ~(MC13892_SWMODE_MASK << MC13892_SWMODE4_SHIFT);
		val |=  MC13892_SWMODE_PWM_PWM << MC13892_SWMODE4_SHIFT;
		mc13xxx_reg_write(mc13xxx, MC13892_REG_SW_5, val);
	} else {
		/* Set switchers in Auto in NORMAL mode & STANDBY mode for Atlas 2.0a */
		/* Setup the switcher mode for SW1 & SW2*/
		mc13xxx_reg_read(mc13xxx, MC13892_REG_SW_4, &val);
		val &= ~(MC13892_SWMODE_MASK << MC13892_SWMODE1_SHIFT);
		val |=  MC13892_SWMODE_AUTO_AUTO << MC13892_SWMODE1_SHIFT;
		val &= ~(MC13892_SWMODE_MASK << MC13892_SWMODE2_SHIFT);
		val |=  MC13892_SWMODE_AUTO_OFF << MC13892_SWMODE2_SHIFT;
		mc13xxx_reg_write(mc13xxx, MC13892_REG_SW_4, val);

		/* Setup the switcher mode for SW3 & SW4 */
		mc13xxx_reg_read(mc13xxx, MC13892_REG_SW_5, &val);
		val &= ~(MC13892_SWMODE_MASK << MC13892_SWMODE3_SHIFT);
		val |=  MC13892_SWMODE_AUTO_AUTO << MC13892_SWMODE3_SHIFT;
		val &= ~(MC13892_SWMODE_MASK << MC13892_SWMODE4_SHIFT);
		val |=  MC13892_SWMODE_AUTO_AUTO << MC13892_SWMODE4_SHIFT;
		mc13xxx_reg_write(mc13xxx, MC13892_REG_SW_5, val);
	}

	/* Set VVIDEO=2.775V, VAUDIO=3V, VSD=3.15V, VGEN1=1.2V, VGEN2=3.15V */
	val = MC13892_SETTING_1_VGEN1_1_2 | MC13892_SETTING_1_VGEN2_3_15;
	val |= MC13892_SETTING_1_VVIDEO_2_775 | MC13892_SETTING_1_VAUDIO_3_0;
	val |= MC13892_SETTING_1_VSD_3_15;
	mc13xxx_reg_write(mc13xxx, MC13892_REG_SETTING_1, val);

	/* Configure VGEN3 and VCAM regulators to use external PNP */
	val = MC13892_MODE_1_VGEN3CONFIG | MC13892_MODE_1_VCAMCONFIG;
	mc13xxx_reg_write(mc13xxx, MC13892_REG_MODE_1, val);
	udelay(200);

	/* Set VGEN3 to 1.8V */
	mc13xxx_reg_read(mc13xxx, MC13892_REG_SETTING_0, &val);
	val &= ~MC13892_SETTING_0_VGEN3_MASK;
	mc13xxx_reg_write(mc13xxx, MC13892_REG_SETTING_0, val);

	/* Enable VGEN3, VCAM, VAUDIO, VVIDEO, VSD regulators */
	val = MC13892_MODE_1_VGEN3EN | MC13892_MODE_1_VGEN3CONFIG;
	val |= MC13892_MODE_1_VCAMEN | MC13892_MODE_1_VCAMCONFIG;
	val |= MC13892_MODE_1_VVIDEOEN | MC13892_MODE_1_VAUDIOEN;
	val |= MC13892_MODE_1_VSDEN;
	mc13xxx_reg_write(mc13xxx, MC13892_REG_MODE_1, val);

	/* Enable USB1 charger */
	val = 0x000409;
	mc13xxx_reg_write(mc13xxx, MC13892_REG_USB1, val);

	/* Set VCOIN to 3.0V and Enable It */
	mc13xxx_reg_read(mc13xxx, MC13892_REG_POWER_CTL0, &val);
	val &= ~(7 << 20);
	val |= (4 << 20) | (1 << 23);
	mc13xxx_reg_write(mc13xxx, MC13892_REG_POWER_CTL0, val);
	/* Keeps VSRTC and CLK32KMCU */
	val |= (1 << 4);
	mc13xxx_reg_write(mc13xxx, MC13892_REG_POWER_CTL0, val);

	/* De-assert reset of external devices on GP01, GPO2, GPO3 and GPO4 */
	mc13xxx_reg_read(mc13xxx, MC13892_REG_POWER_MISC, &val);
	/* GPO1 - External */
	/* GP02 - LAN9221 Power */
	/* GP03 - FEC Reset */
	/* GP04 - Wireless Power */
	if (ccxmx51_id->eth1) {
		val |= MC13892_POWER_MISC_GPO2EN;
		mdelay(100);
	}
	if (ccxmx51_id->eth0)
		val |= MC13892_POWER_MISC_GPO3EN;
	if (ccxmx51_id->wless)
		val |= MC13892_POWER_MISC_GPO4EN;
	mc13xxx_reg_write(mc13xxx, MC13892_REG_POWER_MISC, val);

	udelay(100);

	printf("PMIC initialized.\n");

	console_flush();
	imx51_init_lowlevel(ccxmx51_id->industrial ? 600 : 800);
	clock_notifier_call_chain();
}

static int ccxmx51_is_compatible(void)
{
	return of_machine_is_compatible("digi,connectcore-ccxmx51-som");
}

static void ccxmx51_disable_device(struct device_node *root, const char *label)
{
	struct device_node *np = of_find_node_by_name(root, label);
	if (np)
		of_device_disable(np);
}

static int ccxmx51_board_fixup(struct device_node *root, void *unused)
{
	char *serial;

	if (!ccxmx51_id->eth0)
		ccxmx51_disable_device(root, "ethernet@83fec000");

	if (!ccxmx51_id->eth1)
		ccxmx51_disable_device(root, "lan9221@5,0");

	if (!ccxmx51_id->accel)
		ccxmx51_disable_device(root, "mma7455l@1d");

	serial = basprintf("%08x%08x", 0, boardserial);
	of_set_property(root, "serial-number", serial, strlen(serial) + 1, 1);

	return 0;
}

static int ccxmx51_sdram_fixup(void)
{
	if (!ccxmx51_is_compatible())
		return 0;

	/*
	 * On this board the SDRAM is always configured for 512Mib. The real
	 * size is determined by the board id read from the IIM module.
	 */
	imx_esdctl_disable();

	of_register_fixup(ccxmx51_board_fixup, NULL);

	return 0;
}
postcore_initcall(ccxmx51_sdram_fixup);

static int ccxmx51_init(void)
{
	char manloc;
	u8 hwid[6];

	if (!ccxmx51_is_compatible())
		return 0;

	if ((imx_iim_read(1, 9, hwid, sizeof(hwid)) != sizeof(hwid)) || (hwid[0] < 0x02) || (hwid[0] >= ARRAY_SIZE(ccxmx51_ids)))
		memset(hwid, 0x00, sizeof(hwid));

	ccxmx51_id = &ccxmx51_ids[hwid[0]];
	printf("Module Variant: %s (0x%02x)\n", ccxmx51_id->id_string, hwid[0]);

	if (hwid[0]) {
		printf("Module HW Rev : %02x\n", hwid[1] + 1);

		switch (hwid[2] & 0xc0) {
		case 0x00:
			manloc = 'B';
			break;
		case 0x40:
			manloc = 'W';
			break;
		case 0x80:
			manloc = 'S';
			break;
		default:
			manloc = 'N';
			break;
		}

		eth_register_ethaddr(0, hwid);

		boardserial = ((hwid[2] & 0x3f) << 24) | (hwid[3] << 16) | (hwid[4] << 8) | hwid[5];
		printf("Module Serial : %c%d\n", manloc, boardserial);

		if ((ccxmx51_id->mem_sz - SZ_128M) > 0)
			arm_add_mem_device("ram1", MX51_CSD0_BASE_ADDR + SZ_128M,
					   ccxmx51_id->mem_sz - SZ_128M);

		mc13xxx_register_init_callback(ccxmx51_power_init);
	} else {
		printf("Machine is unsupported. System halted.\n");
		hang();
	}

	barebox_set_model("Digi ConnectCore i.MX51");
	barebox_set_hostname("ccmx51");

	defaultenv_append_directory(defaultenv_ccxmx51);

	return 0;
}
device_initcall(ccxmx51_init);
