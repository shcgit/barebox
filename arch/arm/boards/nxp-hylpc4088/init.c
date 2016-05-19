/*
 * Copyright (C) 2014 Alexander Shiyan <shc_work@mail.ru>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 */

#include <common.h>
#include <driver.h>
#include <gpio.h>
#include <init.h>
#include <io.h>
#include <sizes.h>

#include <asm/armlinux.h>
#include <mach/hardware.h>
#include <i2c/i2c.h>
#include <spi/spi.h>

#if 0
#define SSP0_CS0	(2 * 32 + 19)
#define SSP0_CS1	(2 * 32 + 23)

static const int spi0_cs[] = { SSP0_CS0, SSP0_CS1, -1, };

static const struct spi_board_info hylpc4088_spi_devices[] = {
	{
		/* SST25VF016B */
		.name		= "m25p80",
		.max_speed_hz	= 25000000,
		.bus_num	= 0,
		.chip_select	= 0,
		.mode		= SPI_MODE_0,
		.bits_per_word	= 8,
	}, {
		/* TSC2046 */
		.name		= "tsc2046",
		.max_speed_hz	= 25000000,
		.bus_num	= 0,
		.chip_select	= 1,
		.mode		= SPI_MODE_0,
		.bits_per_word	= 8,
	},
};
#else
#define SSP0_CS0	(2 * 32 + 19)
#define SSP0_CS1	(1 * 32 + 14)

static const int spi0_cs[] = { SSP0_CS0, SSP0_CS1, -1, };

static const struct spi_board_info hylpc4088_spi_devices[] = {
	{
		/* FM25V02 */
		.name		= "m25p80",
		.max_speed_hz	= 40000000,
		.bus_num	= 0,
		.chip_select	= 0,
		.mode		= SPI_MODE_0,
		.bits_per_word	= 8,
	},
	{
		/* ER-OLEDM023-1B */
		.name		= "oledm023",
		.max_speed_hz	= 4000000,
		.bus_num	= 0,
		.chip_select	= 1,
		.mode		= SPI_MODE_0,
		.bits_per_word	= 8,
	}
};
#endif

static int hylpc4088_mem_init(void)
{
	arm_add_mem_device("ram0", LPC4088_DRAM_CS0, SZ_16M);

	return 0;
}
mem_initcall(hylpc4088_mem_init);

static struct resource flash_resources[] = {
	/* Flash */
	DEFINE_RES_MEM(LPC4088_FLASH, SZ_512K),
	/* SRAM */
	DEFINE_RES_MEM(LPC4088_SRAM, SZ_64K),
	/* Boot ROM */
	{
		.start	= LPC4088_BOOTROM,
		.end	= LPC4088_BOOTROM + SZ_8K,
		.flags	= IORESOURCE_MEM | IORESOURCE_READONLY,
	},
};

static struct resource spifi_resources[] = {
	DEFINE_RES_MEM(LPC4088_SPIFI, SZ_16K),
	DEFINE_RES_MEM(LPC4088_SPIFI_BUFFER, SZ_16M),
};

static struct i2c_platform_data hylpc4088_i2c0 = {
	.bitrate = 100000,
};

static int hylpc4088_devices_init(void)
{
	add_generic_device_res("lpc40xx-flash", DEVICE_ID_SINGLE,
			       flash_resources, sizeof(flash_resources), NULL);

	spi_register_board_info(hylpc4088_spi_devices,
				ARRAY_SIZE(hylpc4088_spi_devices));
	add_generic_device("lpc40xx-spi", 0, NULL, LPC4088_SSP0, SZ_16K,
			   IORESOURCE_MEM, &spi0_cs);

	add_generic_device_res("lpc40xx-spifi", DEVICE_ID_SINGLE,
			       spifi_resources, sizeof(spifi_resources), NULL);

	add_generic_device("lpc40xx-i2c", 0, NULL, LPC4088_I2C0,
			   SZ_4K, IORESOURCE_MEM, &hylpc4088_i2c0);

	add_generic_device("lpc40xx-mci", DEVICE_ID_SINGLE, NULL, LPC4088_MCI,
			   SZ_16K, IORESOURCE_MEM, NULL);

	add_generic_device("lpc40xx-ohci", DEVICE_ID_SINGLE, NULL, LPC4088_USB,
			   SZ_4K, IORESOURCE_MEM, NULL);

	devfs_add_partition("flash0", 0, 0, DEVFS_PARTITION_FIXED, "self0");

	return 0;
}
device_initcall(hylpc4088_devices_init);

static int hylpc4088_console_init(void)
{
	devfs_add_partition("mmio-sram", 0, SZ_64K,
			    DEVFS_PARTITION_FIXED, "env0");

	barebox_set_model("HAOYU HY-LPC1788-SDK");

	lpc4088_add_uart(0);

	return 0;
}
console_initcall(hylpc4088_console_init);
