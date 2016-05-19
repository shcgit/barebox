/*
 * Copyright (C) 2014 Alexander Shiyan <shc_work@mail.ru>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 */

#include <common.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <io.h>

#include "clk.h"

#define PLLCON		(0x00)
# define PLLE		BIT(0)
#define PLLCFG		(0x04)
#define PLLSTAT		(0x08)
# define PLLE_STAT	BIT(8)
# define PLOCK		BIT(10)
#define PLLFEED		(0x0c)
# define SEQ0		(0xaa)
# define SEQ1		(0x55)

struct clk_pll {
	struct clk	clk;
	const char	*parent;
	void __iomem	*base;
};

#define to_clk_pll(_hw) container_of(_hw, struct clk_pll, clk)

static inline void clk_pll_feed(struct clk_pll *pll, unsigned int wait)
{
	writel(SEQ0, pll->base + PLLFEED);
	writel(SEQ1, pll->base + PLLFEED);

	while (wait) {
		if (readl(pll->base + PLLSTAT) & PLOCK)
			break;
	}
}

static int clk_pll_enable(struct clk *clk)
{
	struct clk_pll *pll = to_clk_pll(clk);

	writel(PLLE, pll->base + PLLCON);

	clk_pll_feed(pll, 1);

	return 0;
}

static void clk_pll_disable(struct clk *clk)
{
	struct clk_pll *pll = to_clk_pll(clk);

	writel(0, pll->base + PLLCON);

	clk_pll_feed(pll, 0);
}

static int clk_pll_is_enabled(struct clk *clk)
{
	struct clk_pll *pll = to_clk_pll(clk);

	return !!(readl(pll->base + PLLSTAT) & PLLE_STAT);
}

static unsigned long clk_pll_recalc_rate(struct clk *clk,
					 unsigned long parent_rate)
{
	struct clk_pll *pll = to_clk_pll(clk);
	u32 pllstat = readl(pll->base + PLLSTAT);
	u32 msel = (pllstat & 0x1f) + 1;

	return parent_rate * msel;
}

static long clk_pll_round_rate(struct clk *clk, unsigned long rate,
			       unsigned long *prate)
{
	u32 psel, msel = *prate / rate;
	unsigned long fcco;

	if ((msel < 0x1) || (msel > 0x20))
		return -EINVAL;

	for (psel = 0; psel <= 3; psel++) {
		fcco = *prate * 2 * (1 << psel);
		if ((fcco >= 156000000) && (fcco <= 320000000))
			return *prate * msel;
	}

	return -EINVAL;
}

static int clk_pll_set_rate(struct clk *clk, unsigned long rate,
			    unsigned long parent_rate)
{
	struct clk_pll *pll = to_clk_pll(clk);
	u32 psel, msel = parent_rate / rate;
	unsigned long fcco;

	if ((msel < 0x1) || (msel > 0x20))
		return -EINVAL;

	for (psel = 0; psel <= 3; psel++) {
		fcco = parent_rate * 2 * (1 << psel);
		if ((fcco >= 156000000) && (fcco <= 320000000)) {
			msel |= psel << 5;
			writel(msel, pll->base + PLLCFG);
			clk_pll_feed(pll, clk_pll_is_enabled(clk));
			return 0;
		}
	}

	return -EINVAL;
}

static const struct clk_ops clk_pll_ops = {
	.enable		= clk_pll_enable,
	.disable	= clk_pll_disable,
	.is_enabled	= clk_pll_is_enabled,
	.recalc_rate	= clk_pll_recalc_rate,
	.round_rate	= clk_pll_round_rate,
	.set_rate	= clk_pll_set_rate,
};

struct clk *lpc_clk_pll(const char *name, const char *parent_name,
			void __iomem *base)
{
	struct clk_pll *pll;
	int ret;

	pll = xzalloc(sizeof(*pll));
	if (!pll)
		return ERR_PTR(-ENOMEM);

	pll->parent = parent_name;
	pll->clk.name = name;
	pll->clk.ops = &clk_pll_ops;
	pll->clk.parent_names = &pll->parent;
	pll->clk.num_parents = 1;

	pll->base = base;

	ret = clk_register(&pll->clk);
	if (ret)
		ERR_PTR(ret);

	return &pll->clk;
}
