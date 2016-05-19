/*
 * Copyright (C) 2015 Alexander Shiyan <shc_work@mail.ru>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 */

#include <common.h>
#include <init.h>
#include <io.h>
#include <ns16550.h>

#include <linux/sizes.h>
#include <mach/hardware.h>

#define WDT_MOD		(LPC4088_WDT + 0x00)
# define WDEN		BIT(0)
# define WDRESET	BIT(1)
#define WDT_FEED	(LPC4088_WDT + 0x08)

void __noreturn reset_cpu(unsigned long addr)
{
	shutdown_barebox();

	/* Enable watchdog timer */
	writel(WDEN, WDT_MOD);
	writel(0xaa, WDT_FEED);
	writel(0x55, WDT_FEED);

	/* Enable watchdog reset */
	writel(WDEN | WDRESET, WDT_MOD);

	/* Initiate immediate reset by wrong sequence */
	writel(0xaa, WDT_FEED);
	writel(0xff, WDT_FEED);

	hang();
}

static const resource_size_t LPC4088_UART[] = {
	LPC4088_UART0, LPC4088_UART1, LPC4088_UART2, LPC4088_UART3,
};

struct device_d *lpc4088_add_uart(const unsigned int idx)
{
	return add_generic_device("lpc40xx-uart", idx, NULL, LPC4088_UART[idx],
				  SZ_16K, IORESOURCE_MEM | IORESOURCE_MEM_32BIT,
				  NULL);
}

static int lpc408x_soc_init(void)
{
	unsigned i;

	add_generic_device("lpc40xx-clkctrl", DEVICE_ID_SINGLE, NULL,
			   LPC4088_SYSCTRL, SZ_1K, IORESOURCE_MEM, NULL);
	add_generic_device("lpc40xx-clocksource", DEVICE_ID_SINGLE, NULL,
			   LPC4088_SYSTIMER, SZ_32, IORESOURCE_MEM, NULL);

	for (i = 0; i <= 5; i++)
		add_generic_device("lpc40xx-gpio", i, NULL,
				   LPC4088_GPIO + i * SZ_32, SZ_32,
				   IORESOURCE_MEM, NULL);

	return 0;
}
postcore_initcall(lpc408x_soc_init);
