// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2010-2012 Broadcom. All rights reserved.
 * Copyright (c) 2013 Lubomir Rintel
 */

#include <linux/hw_random.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/reset.h>
#include <of.h>
#include <linux/device.h>
#include <linux/printk.h>
#include <linux/clk.h>
#include <linux/reset.h>

#define RNG_CTRL	0x0
#define RNG_STATUS	0x4
#define RNG_DATA	0x8
#define RNG_INT_MASK	0x10

/* enable rng */
#define RNG_RBGEN	0x1

/* the initial numbers generated are "less random" so will be discarded */
#define RNG_WARMUP_COUNT 0x40000

#define RNG_INT_OFF	0x1

struct bcm2835_rng_priv {
	struct hwrng rng;
	void __iomem *base;
	bool mask_interrupts;
	struct clk *clk;
	struct reset_control *reset;
};

static inline struct bcm2835_rng_priv *to_rng_priv(struct hwrng *rng)
{
	return container_of(rng, struct bcm2835_rng_priv, rng);
}

static inline u32 rng_readl(struct bcm2835_rng_priv *priv, u32 offset)
{
	/* MIPS chips strapped for BE will automagically configure the
	 * peripheral registers for CPU-native byte order.
	 */
	if (IS_ENABLED(CONFIG_MIPS) && IS_ENABLED(CONFIG_CPU_BIG_ENDIAN))
		return __raw_readl(priv->base + offset);
	else
		return readl(priv->base + offset);
}

static inline void rng_writel(struct bcm2835_rng_priv *priv, u32 val,
			      u32 offset)
{
	if (IS_ENABLED(CONFIG_MIPS) && IS_ENABLED(CONFIG_CPU_BIG_ENDIAN))
		__raw_writel(val, priv->base + offset);
	else
		writel(val, priv->base + offset);
}

static int bcm2835_rng_read(struct hwrng *rng, void *buf, size_t max,
			       bool wait)
{
	struct bcm2835_rng_priv *priv = to_rng_priv(rng);
	u32 max_words = max / sizeof(u32);
	u32 num_words, count;

	while ((rng_readl(priv, RNG_STATUS) >> 24) == 0) {
		if (!wait)
			return 0;
		hwrng_yield(rng);
	}

	num_words = rng_readl(priv, RNG_STATUS) >> 24;
	if (num_words > max_words)
		num_words = max_words;

	for (count = 0; count < num_words; count++)
		((u32 *)buf)[count] = rng_readl(priv, RNG_DATA);

	return num_words * sizeof(u32);
}

static int bcm2835_rng_init(struct hwrng *rng)
{
	struct bcm2835_rng_priv *priv = to_rng_priv(rng);
	int ret = 0;
	u32 val;

	ret = clk_prepare_enable(priv->clk);
	if (ret)
		return ret;

	ret = reset_control_reset(priv->reset);
	if (ret)
		return ret;

	if (priv->mask_interrupts) {
		/* mask the interrupt */
		val = rng_readl(priv, RNG_INT_MASK);
		val |= RNG_INT_OFF;
		rng_writel(priv, val, RNG_INT_MASK);
	}

	/* set warm-up count & enable */
	rng_writel(priv, RNG_WARMUP_COUNT, RNG_STATUS);
	rng_writel(priv, RNG_RBGEN, RNG_CTRL);

	return ret;
}

static void bcm2835_rng_cleanup(struct bcm2835_rng_priv *priv)
{
	/* disable rng hardware */
	rng_writel(priv, 0, RNG_CTRL);

	clk_disable_unprepare(priv->clk);
}

struct bcm2835_rng_of_data {
	bool mask_interrupts;
};

static const struct bcm2835_rng_of_data nsp_rng_of_data = {
	.mask_interrupts = true,
};

static const struct of_device_id bcm2835_rng_of_match[] = {
	{ .compatible = "brcm,bcm2835-rng"},
	{ .compatible = "brcm,bcm-nsp-rng", .data = &nsp_rng_of_data },
	{ .compatible = "brcm,bcm5301x-rng", .data = &nsp_rng_of_data },
	{ .compatible = "brcm,bcm6368-rng"},
	{},
};

static int bcm2835_rng_probe(struct device *dev)
{
	const struct bcm2835_rng_of_data *of_data;
	struct bcm2835_rng_priv *priv;
	int err;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	/* map peripheral */
	priv->base = dev_platform_ioremap_resource(dev, 0);
	if (IS_ERR(priv->base))
		return PTR_ERR(priv->base);

	/* Clock is optional on most platforms */
	priv->clk = clk_get_optional(dev, NULL);
	if (IS_ERR(priv->clk))
		return PTR_ERR(priv->clk);

	priv->reset = reset_control_get_optional(dev, NULL);
	if (IS_ERR(priv->reset))
		return PTR_ERR(priv->reset);

	priv->rng.name = dev_name(dev);
	priv->rng.init = bcm2835_rng_init;
	priv->rng.read = bcm2835_rng_read;

	of_data = device_get_match_data(dev);
	if (of_data)
		priv->mask_interrupts = of_data->mask_interrupts;

	/* register driver */
	err = hwrng_register(dev, &priv->rng);
	if (err)
		dev_err(dev, "hwrng registration failed\n");
	else
		dev_info(dev, "hwrng registered\n");

	dev->priv = priv;

	return err;
}

static void bcm2835_rng_remove(struct device *dev)
{
	bcm2835_rng_cleanup(dev->priv);
}

MODULE_DEVICE_TABLE(of, bcm2835_rng_of_match);

static struct driver bcm2835_rng_driver = {
	.name		= "bcm2835-rng",
	.of_match_table = bcm2835_rng_of_match,
	.probe		= bcm2835_rng_probe,
	.remove		= bcm2835_rng_remove,
};
device_platform_driver(bcm2835_rng_driver);

MODULE_AUTHOR("Lubomir Rintel <lkundrak@v3.sk>");
MODULE_DESCRIPTION("BCM2835 Random Number Generator (RNG) driver");
MODULE_LICENSE("GPL v2");
