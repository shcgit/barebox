/*
 * Copyright (C) 2014 Alexander Shiyan <shc_work@mail.ru>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 */

#ifndef __LPC408X_CLK_H
#define __LPC408X_CLK_H

struct clk *lpc_clk_pll(const char *, const char *, void __iomem *);

static inline struct clk *lpc_clk_fix(const char *name, int rate)
{
	return clk_fixed(name, rate);
}

static inline struct clk *lpc_clk_gate(const char *name,
				       const char *parent_name,
				       void __iomem *reg, u8 shift)
{
	return clk_gate(name, parent_name, reg, shift, 0, 0);
}

static inline struct clk *lpc_clk_mux(const char *name, void __iomem *reg,
				      u8 shift, u8 width,
				      const char **parents, int nparents)
{
	return clk_mux(name, reg, shift, width, parents, nparents, 0);
}

static inline struct clk *lpc_clk_div0(const char *name, const char *parent,
				       void __iomem *reg, u8 shift, u8 width)
{

	return clk_divider(name, parent, reg, shift, width, 0);
}

static inline struct clk *lpc_clk_div1(const char *name, const char *parent,
				       void __iomem *reg, u8 shift, u8 width)
{

	return clk_divider_one_based(name, parent, reg, shift, width, 0);
}

#endif
