/*
 * Copyright (C) 2018 Sascha Hauer, Pengutronix
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
 * Foundation.
 *
 */

#include <common.h>
#include <init.h>
#include <asm/memory.h>
#include <linux/sizes.h>

static int imx8mq_evk_mem_init(void)
{
	arm_add_mem_device("ram0", 0x40000000, SZ_2G);

	request_sdram_region("ATF", 0x40000000, SZ_128K);

	return 0;
}
mem_initcall(imx8mq_evk_mem_init);

static int nxp_imx8mq_evk_init(void)
{
	if (!of_machine_is_compatible("fsl,imx8mq-evk"))
		return 0;

	barebox_set_hostname("imx8mq-evk");

	return 0;
}
device_initcall(nxp_imx8mq_evk_init);
