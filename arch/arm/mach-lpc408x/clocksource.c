/*
 * Copyright (C) 2014 Alexander Shiyan <shc_work@mail.ru>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 */

#include <clock.h>
#include <common.h>
#include <init.h>
#include <io.h>
#include <linux/clk.h>
#include <linux/err.h>

#define STCTRL		(0x10)
# define STCTRL_ENABLE	(1 << 0)
# define STCTRL_CLKSRC	(1 << 2)
#define STRELOAD	(0x14)
#define STCURR		(0x18)

static void __iomem *lpc40xx_base;

static uint64_t lpc40xx_clocksource_read(void)
{
	return 0 - readl(lpc40xx_base + STCURR);
}

static struct clocksource lpc40xx_cs = {
	.read	= lpc40xx_clocksource_read,
	.mask	= CLOCKSOURCE_MASK(24),
};

static int lpc40xx_clocksource_probe(struct device_d *dev)
{
	struct clk *clk;

	clk = clk_get(dev, NULL);
	if (IS_ERR(clk))
		return PTR_ERR(clk);

	lpc40xx_base = dev_request_mem_region(dev, 0);
	if (IS_ERR(lpc40xx_base)) {
		clk_put(clk);
		return PTR_ERR(lpc40xx_base);
	}

	writel(0x00ffffff, lpc40xx_base + STRELOAD);
	writel(STCTRL_ENABLE | STCTRL_CLKSRC, lpc40xx_base + STCTRL);

	clocks_calc_mult_shift(&lpc40xx_cs.mult, &lpc40xx_cs.shift,
			       clk_get_rate(clk), NSEC_PER_SEC, 10);

	return init_clock(&lpc40xx_cs);
}

static struct driver_d lpc40xx_clocksource_driver = {
	.probe	= lpc40xx_clocksource_probe,
	.name	= "lpc40xx-clocksource",
};

static int lpc40xx_clocksource_init(void)
{
	return platform_driver_register(&lpc40xx_clocksource_driver);
}
postcore_initcall(lpc40xx_clocksource_init);
