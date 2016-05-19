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
#include <init.h>
#include <io.h>
#include <linux/clk.h>
#include <linux/clkdev.h>
#include <linux/err.h>

#include "clk.h"

#define PLLCON0		(regs + 0x0080)
#define PLLCON1		(regs + 0x00a0)
#define EMCDIV		(regs + 0x0100)
#define CCLK		(regs + 0x0104)
#define USBDIV		(regs + 0x0108)
#define CLKSRC		(regs + 0x010c)
#define PCLK		(regs + 0x01a8)
#define SPIFIDIV	(regs + 0x01b4)
#define CLKOUT		(regs + 0x01c8)

static const char *sel_sysclk[] = { "irc_clk", "osc_clk", };
static const char *sel_cpuclk[] = { "sysclk", "pll_clk", };
static const char *sel_usbspifi[] = { "sysclk", "pll_clk", "alt_pll_clk", };
static const char *sel_clkout[] = { "cclk", "osc_clk", "irc_clk",  "usb_clk",
				    "rtc", "spifi_clk", "wdt", };

enum {
	dummy, rtc, wdt, irc_clk, osc_clk, sysclk, pll_clk, alt_pll_clk, cpu_sel,
	cclk, emc_clk, pclk, usb_sel, usb_clk, spifi_sel, spifi_clk, clkout_sel,
	clkout_div, clkout_gate, clk_max
};

static struct clk *clks[clk_max];

static int lpc40xx_ccm_probe(struct device_d *dev)
{
	void __iomem *regs;
	unsigned i;

	regs = dev_request_mem_region(dev, 0);
	if (IS_ERR(regs))
		return PTR_ERR(regs);

	clks[dummy] = lpc_clk_fix("dummy", 0);
	clks[rtc] = lpc_clk_fix("rtc", 32768);
	clks[wdt] = lpc_clk_fix("wdt", 500000);
	clks[irc_clk] = lpc_clk_fix("irc_clk", 12000000);
	clks[osc_clk] = lpc_clk_fix("osc_clk", 12000000);
	clks[sysclk] = lpc_clk_mux("sysclk", CLKSRC, 0, 1, sel_sysclk, ARRAY_SIZE(sel_sysclk));
	clks[pll_clk] = lpc_clk_pll("pll_clk", "sysclk", PLLCON0);
	clks[alt_pll_clk] = lpc_clk_pll("alt_pll_clk", "osc_clk", PLLCON1);
	clks[cpu_sel] = lpc_clk_mux("cpu_sel", CCLK, 8, 1, sel_cpuclk, ARRAY_SIZE(sel_cpuclk));
	clks[cclk] = lpc_clk_div1("cclk", "cpu_sel", CCLK, 0, 5);
	clks[emc_clk] = lpc_clk_div0("emc_clk", "cclk", EMCDIV, 0, 1);
	clks[pclk] = lpc_clk_div1("pclk", "cpu_sel", PCLK, 0, 3);
	clks[usb_sel] = lpc_clk_mux("usb_sel", USBDIV, 8, 2, sel_usbspifi, ARRAY_SIZE(sel_usbspifi));
	clks[usb_clk] = lpc_clk_div1("usb_clk", "usb_sel", USBDIV, 0, 2);
	clks[spifi_sel] = lpc_clk_mux("spifi_sel", SPIFIDIV, 8, 2, sel_usbspifi, ARRAY_SIZE(sel_usbspifi));
	clks[spifi_clk] = lpc_clk_div1("spifi_clk", "spifi_sel", SPIFIDIV, 0, 5);
	clks[clkout_sel] = lpc_clk_mux("clkout_sel", CLKOUT, 0, 4, sel_clkout, ARRAY_SIZE(sel_clkout));
	clks[clkout_div] = lpc_clk_div0("clkout_div", "clkout_sel", CLKOUT, 4, 4);
	clks[clkout_gate] = lpc_clk_gate("clkout_gate", "clkout_div", CLKOUT, 8);

	clk_set_rate(clks[alt_pll_clk], 144000000);
	clk_set_parent(clks[usb_sel], clks[alt_pll_clk]);
	clk_set_rate(clks[usb_clk], 48000000);
	clk_set_parent(clks[spifi_sel], clks[alt_pll_clk]);

	for (i = 0; i < ARRAY_SIZE(clks); i++)
		clk_register_clkdev(clks[i], clks[i]->name, NULL);

	clk_add_alias(NULL, "lpc40xx-clocksource", "cclk", NULL);
	clk_add_alias(NULL, "lpc40xx-flash", "cclk", NULL);
	clk_add_alias(NULL, "lpc40xx-i2c0", "pclk", NULL);
	clk_add_alias(NULL, "lpc40xx-spi0", "pclk", NULL);
	clk_add_alias(NULL, "lpc40xx-spifi", "spifi_clk", NULL);
	clk_add_alias(NULL, "lpc40xx-mci", "pclk", NULL);

	clk_add_alias(NULL, "lpc40xx-uart0", "pclk", NULL);
	clk_add_alias(NULL, "lpc40xx-uart1", "pclk", NULL);
	clk_add_alias(NULL, "lpc40xx-uart2", "pclk", NULL);
	clk_add_alias(NULL, "lpc40xx-uart3", "pclk", NULL);

	return 0;
}

static struct driver_d lpc40xx_ccm_driver = {
	.probe	= lpc40xx_ccm_probe,
	.name	= "lpc40xx-clkctrl",
};

static int lpc40xx_ccm_init(void)
{
	return platform_driver_register(&lpc40xx_ccm_driver);
}
postcore_initcall(lpc40xx_ccm_init);
