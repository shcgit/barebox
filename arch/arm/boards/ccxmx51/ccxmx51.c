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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */

#include <common.h>
#include <init.h>
#include <net.h>
#include <notifier.h>
#include <sizes.h>
#include <asm/armlinux.h>
#include <mach/esdctl.h>
#include <mach/generic.h>
#include <mach/iim.h>
#include <mach/imx5.h>
#include <mach/imx51-regs.h>
#include <mach/revision.h>
#include <mfd/mc13xxx.h>

static const struct ccxmx51_ident {
	char		*id_string;
	unsigned int	mem_sz;
	unsigned char	industrial;
	unsigned char	eth0;
	unsigned char	eth1;
	unsigned char	wless;
} *ccxmx51_id, ccxmx51_ids[] = {
/* 0x00 */	{ "Unknown",						0,       0, 0, 0, 0 },
/* 0x01 */	{ "Not supported",					0,       0, 0, 0, 0 },
/* 0x02 */	{ "i.MX515@800MHz, Wireless, PHY, Ext. Eth, Accel",	SZ_512M, 0, 1, 1, 1 },
/* 0x03 */	{ "i.MX515@800MHz, PHY, Ext. Eth, Accel",		SZ_512M, 0, 1, 1, 0 },
/* 0x04 */	{ "i.MX515@600MHz, Wireless, PHY, Ext. Eth, Accel",	SZ_512M, 1, 1, 1, 1 },
/* 0x05 */	{ "i.MX515@600MHz, PHY, Ext. Eth, Accel",		SZ_512M, 1, 1, 1, 0 },
/* 0x06 */	{ "i.MX515@800MHz, Wireless, PHY, Accel",		SZ_512M, 0, 1, 0, 1 },
/* 0x07 */	{ "i.MX515@800MHz, PHY, Accel",				SZ_512M, 0, 1, 0, 0 },
/* 0x08 */	{ "i.MX515@800MHz, Wireless, PHY, Accel",		SZ_256M, 0, 1, 0, 1 },
/* 0x09 */	{ "i.MX515@800MHz, PHY, Accel",				SZ_256M, 0, 1, 0, 0 },
/* 0x0a */	{ "i.MX515@600MHz, Wireless, PHY, Accel",		SZ_256M, 1, 1, 0, 1 },
/* 0x0b */	{ "i.MX515@600MHz, PHY, Accel",				SZ_256M, 1, 1, 0, 0 },
/* 0x0c */	{ "i.MX515@800MHz, Wireless, PHY, Accel",		SZ_128M, 0, 1, 0, 1 },
/* 0x0d */	{ "i.MX512@800MHz",					SZ_128M, 0, 0, 0, 0 },
/* 0x0e */	{ "i.MX515@800MHz, Wireless, PHY, Accel",		SZ_512M, 0, 1, 0, 1 },
/* 0x0f */	{ "i.MX515@600MHz, PHY, Accel",				SZ_128M, 1, 1, 0, 0 },
/* 0x10 */	{ "i.MX515@600MHz, Wireless, PHY, Accel",		SZ_128M, 1, 1, 0, 1 },
/* 0x11 */	{ "i.MX515@800MHz, PHY, Accel",				SZ_128M, 0, 1, 0, 0 },
/* 0x12 */	{ "i.MX515@600MHz, Wireless, PHY, Accel",		SZ_512M, 1, 1, 0, 1 },
/* 0x13 */	{ "i.MX515@800MHz, PHY, Accel",				SZ_512M, 0, 1, 0, 0 },
};

static int ccxmx51_power_init(void)
{
	struct mc13xxx *mc13xxx_dev;
	u32 val;

	mc13xxx_dev = mc13xxx_get();
	if (!mc13xxx_dev)
		return -ENODEV;

	mc13xxx_reg_read(mc13xxx_dev, MC13892_REG_POWER_MISC, &val);
	/* Reset devices by clearing GP01-GPO4 */
	val &= ~((1 << 21) | (3 << 12) | (3 << 10) | (3 << 8) | (3 << 6));
	/* Switching off the PWGT1SPIEN */
	val |= (1 << 15);
	/* Switching on the PWGT2SPIEN */
	val &= ~(1 << 16);
	/* Enable short circuit protection */
	val |= (1 << 0);
	mc13xxx_reg_write(mc13xxx_dev, MC13892_REG_POWER_MISC, val);

	/* Allow charger to charge (4.2V and 560mA) */
	val = 0x238033;
	mc13xxx_reg_write(mc13xxx_dev, MC13892_REG_CHARGE, val);

	if (imx_silicon_revision() < IMX_CHIP_REV_3_0) {
		/* Set core voltage (SW1) to 1.1V */
		mc13xxx_reg_read(mc13xxx_dev, MC13892_REG_SW_0, &val);
		val &= ~0x00001f;
		val |=  0x000014;
		mc13xxx_reg_write(mc13xxx_dev, MC13892_REG_SW_0, val);

		/* Setup VCC (SW2) to 1.25 */
		mc13xxx_reg_read(mc13xxx_dev, MC13892_REG_SW_1, &val);
		val &= ~0x00001f;
		val |=  0x00001a;
		mc13xxx_reg_write(mc13xxx_dev, MC13892_REG_SW_1, val);

		/* Setup 1V2_DIG1 (SW3) to 1.25 */
		mc13xxx_reg_read(mc13xxx_dev, MC13892_REG_SW_2, &val);
		val &= ~0x00001f;
		val |=  0x00001a;
		mc13xxx_reg_write(mc13xxx_dev, MC13892_REG_SW_2, val);
	} else {
		/* Setup VCC (SW2) to 1.225 */
		mc13xxx_reg_read(mc13xxx_dev, MC13892_REG_SW_1, &val);
		val &= ~0x00001f;
		val |=  0x000019;
		mc13xxx_reg_write(mc13xxx_dev, MC13892_REG_SW_1, val);

		/* Setup 1V2_DIG1 (SW3) to 1.2 */
		mc13xxx_reg_read(mc13xxx_dev, MC13892_REG_SW_2, &val);
		val &= ~0x00001f;
		val |=  0x000018;
		mc13xxx_reg_write(mc13xxx_dev, MC13892_REG_SW_2, val);
	}

	if (mc13xxx_revision(mc13xxx_dev) <= MC13892_REVISION_2_0) {
		/* Set switchers in PWM mode for Atlas 2.0 and lower */
		/* Setup the switcher mode for SW1 & SW2*/
		mc13xxx_reg_read(mc13xxx_dev, MC13892_REG_SW_4, &val);
		val &= ~0x003c0f;
		val |=  0x001405;
		mc13xxx_reg_write(mc13xxx_dev, MC13892_REG_SW_4, val);

		/* Setup the switcher mode for SW3 & SW4 */
		mc13xxx_reg_read(mc13xxx_dev, MC13892_REG_SW_5, &val);
		val &= ~0x000f0f;
		val |=  0x000505;
		mc13xxx_reg_write(mc13xxx_dev, MC13892_REG_SW_5, val);
	} else {
		/* Set switchers in Auto in NORMAL mode & STANDBY mode for Atlas 2.0a */
		/* Setup the switcher mode for SW1 & SW2*/
		mc13xxx_reg_read(mc13xxx_dev, MC13892_REG_SW_4, &val);
		val &= ~0x003c0f;
		val |=  0x002008;
		mc13xxx_reg_write(mc13xxx_dev, MC13892_REG_SW_4, val);

		/* Setup the switcher mode for SW3 & SW4 */
		mc13xxx_reg_read(mc13xxx_dev, MC13892_REG_SW_5, &val);
		val &= ~0x000f0f;
		val |=  0x000808;
		mc13xxx_reg_write(mc13xxx_dev, MC13892_REG_SW_5, val);
	}

	/* Set VVIDEO to 2.775V, VAUDIO to 3V, VSD to 3.15V */
	mc13xxx_reg_read(mc13xxx_dev, MC13892_REG_SETTING_1, &val);
	val &= ~0x0001fc;
	val |=  0x0001f4;
	mc13xxx_reg_write(mc13xxx_dev, MC13892_REG_SETTING_1, val);

	/* Configure VGEN3 and VCAM regulators to use external PNP */
	val = 0x000208;
	mc13xxx_reg_write(mc13xxx_dev, MC13892_REG_MODE_1, val);
	udelay(200);

	/* Set VGEN3 to 1.8V */
	mc13xxx_reg_read(mc13xxx_dev, MC13892_REG_SETTING_0, &val);
	val &= ~(1 << 14);
	mc13xxx_reg_write(mc13xxx_dev, MC13892_REG_SETTING_0, val);

	/* Enable VGEN3, VCAM, VAUDIO, VVIDEO, VSD regulators */
	val = 0x049249;
	mc13xxx_reg_write(mc13xxx_dev, MC13892_REG_MODE_1, val);

	/* Enable USB1 charger */
	val = 0x000409;
	mc13xxx_reg_write(mc13xxx_dev, MC13892_REG_USB1, val);

	/* Set VCOIN to 3.0V and Enable It */
	mc13xxx_reg_read(mc13xxx_dev, MC13892_REG_POWER_CTL0, &val);
	val &= ~(7 << 20);
	val |= (4 << 20) | (1 << 23);
	mc13xxx_reg_write(mc13xxx_dev, MC13892_REG_POWER_CTL0, val);
	/* Keeps VSRTC and CLK32KMCU */
	val |= (1 << 4);
	mc13xxx_reg_write(mc13xxx_dev, MC13892_REG_POWER_CTL0, val);

	/* De-assert reset of external devices on GP01, GPO2, GPO3 and GPO4 */
	mc13xxx_reg_read(mc13xxx_dev, MC13892_REG_POWER_MISC, &val);
	/* GPO1 - External */
	/* GP02 - LAN9221 Power */
	/* GP03 - FEC Reset */
	/* GP04 - Wireless Power */
	if (IS_ENABLED(CONFIG_DRIVER_NET_SMC911X) && ccxmx51_id->eth1) {
		val |= (1 << 8);
		mdelay(100);
	}
	if (IS_ENABLED(CONFIG_DRIVER_NET_FEC_IMX) && ccxmx51_id->eth0)
		val |= (1 << 10);
	if (ccxmx51_id->wless)
		val |= (1 << 12);
	mc13xxx_reg_write(mc13xxx_dev, MC13892_REG_POWER_MISC, val);

	udelay(100);

	return 0;
}

static int ccxmx51_is_compatible(void)
{
	return of_machine_is_compatible("digi,connectcore-ccxmx51-som");
}

/*
 * On this board the SDRAM is always configured for 512Mib. The real
 * size is determined by the board id read from the IIM module.
 */
static int ccxmx51_sdram_fixup(void)
{
	if (ccxmx51_is_compatible())
		imx_esdctl_disable();

	return 0;
}
postcore_initcall(ccxmx51_sdram_fixup);

static int ccxmx51_devices_init(void)
{
	u8 hwid[6];
	char manloc;

	if (!ccxmx51_is_compatible())
		return -ENOSYS;

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
		printf("Module Serial : %c%d\n", manloc, ((hwid[2] & 0x3f) << 24) | (hwid[3] << 16) | (hwid[4] << 8) | hwid[5]);
		if ((ccxmx51_id->mem_sz - SZ_128M) > 0)
			arm_add_mem_device("ram1", MX51_CSD0_BASE_ADDR + SZ_128M, ccxmx51_id->mem_sz - SZ_128M);
	} else
		return -ENOSYS;

	if (ccxmx51_power_init()) {
		printf("Could not setup PMIC. System unusable.\n");
		return -ENODEV;
	}

	console_flush();
	imx51_init_lowlevel(ccxmx51_id->industrial ? 600 : 800);
	clock_notifier_call_chain();

	eth_register_ethaddr(0, hwid);

	return 0;
}
device_initcall(ccxmx51_devices_init);
