/*
 * Copyright (C) 2015 Alexander Shiyan <shc_work@mail.ru>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 */

#include <clock.h>
#include <common.h>
#include <init.h>
#include <malloc.h>
#include <linux/clk.h>
#include <linux/err.h>

#include <io.h>
#include <i2c/i2c.h>

/* Registers and bit definitions */
#define I2C_CONSET	0x00
# define CONSET_AA	BIT(2)
# define CONSET_SI	BIT(3)
# define CONSET_STO	BIT(4)
# define CONSET_STA	BIT(5)
# define CONSET_I2EN	BIT(6)
#define I2C_STAT	0x04
#define I2C_DAT		0x08
#define I2C_SCLH	0x10
#define I2C_SCLL	0x14
#define I2C_CONCLR	0x18
# define CONCLR_AAC	BIT(2)
# define CONCLR_SIC	BIT(3)
# define CONCLR_STAC	BIT(5)
# define CONCLR_I2ENC	BIT(6)
#define I2C_MMCTRL	0x1c

/* Status codes */
#define STAT_START	0x08
#define STAT_RSTART	0x10
#define STAT_SLAW_ACK	0x18
#define STAT_DATAW_ACK	0x28
#define STAT_SLAR_ACK	0x40
#define STAT_DATAR_ACK	0x50
#define STAT_DATAR_NACK	0x58

struct lpc40xx_i2c {
	struct i2c_adapter	adapter;
	struct clk		*clk;
	void __iomem		*regs;
	int			repstart;
};

static int lpc40xx_i2c_busy(struct lpc40xx_i2c *i2c)
{
	uint64_t start = get_time_ns();

	do {
		if (readl(i2c->regs + I2C_CONSET) & CONSET_SI)
			return 0;
	} while (!is_timeout(start, SECOND >> 1));

	return -ETIMEDOUT;
}

static int lpc40xx_i2c_status(struct lpc40xx_i2c *i2c)
{
	return readl(i2c->regs + I2C_STAT) & 0xf8;
}

static int lpc40xx_i2c_start(struct lpc40xx_i2c *i2c)
{
	int ret;

	/* Clear flags */
	writel(CONCLR_AAC | CONCLR_SIC | CONCLR_STAC, i2c->regs + I2C_CONCLR);

	/* Enable I2C controller */
	writel(CONSET_I2EN, i2c->regs + I2C_CONSET);

	/* Start flag */
	writel(CONSET_STA, i2c->regs + I2C_CONSET);

	ret = lpc40xx_i2c_busy(i2c);
	if (ret)
		return ret;

	if (lpc40xx_i2c_status(i2c) == STAT_START)
		return 0;

	return -EIO;
}

static void lpc40xx_i2c_stop(struct lpc40xx_i2c *i2c)
{
	/* Stop flag */
	writel(CONSET_STO, i2c->regs + I2C_CONSET);

	lpc40xx_i2c_busy(i2c);

	/* Disable I2C controller */
	writel(CONCLR_I2ENC, i2c->regs + I2C_CONCLR);
}

static int lpc40xx_i2c_xfer_one(struct lpc40xx_i2c *i2c, struct i2c_msg *msg)
{
	int ret, status, i;
	u8 addr, mode;

	if (msg->flags & (I2C_M_DATA_ONLY | I2C_M_TEN))
		return -ENOTSUPP;

//pr_info("0 Addr:0x%02x Flags: 0x%02x len %i\n", msg->addr, msg->flags, msg->len);
	addr = msg->addr << 1;
	mode = (msg->flags & I2C_M_RD) ? BIT(0) : 0;

	if ((i2c->repstart > -1) && (mode != i2c->repstart)) {
		/* Repeated start */
		writel(CONSET_STA, i2c->regs + I2C_CONSET);
		writel(CONCLR_SIC, i2c->regs + I2C_CONCLR);

		ret = lpc40xx_i2c_busy(i2c);
		if (ret)
			return ret;

		status = lpc40xx_i2c_status(i2c);
//pr_info("R Addr:0x%02x Status:0x%02x Data:0x%02x\n", msg->addr, status, readl(i2c->regs + I2C_DAT));
		if (status != STAT_RSTART)
			return -EIO;
	}

	i2c->repstart = mode;

	writel(addr | mode, i2c->regs + I2C_DAT);

	writel(CONCLR_STAC | CONCLR_SIC, i2c->regs + I2C_CONCLR);

	ret = lpc40xx_i2c_busy(i2c);
	if (ret)
		return ret;

	status = lpc40xx_i2c_status(i2c);
//pr_info("1 Addr:0x%02x Status:0x%02x Data:0x%02x\n", msg->addr, status, readl(i2c->regs + I2C_DAT));
	if ((status != STAT_SLAW_ACK) && (status != STAT_SLAR_ACK))
		return -ENODEV;

	for (i = 0; i < msg->len; i++) {
		if (msg->flags & I2C_M_RD) {
			bool last = i == (msg->len - 1);

			if (!last)
				writel(CONSET_AA, i2c->regs + I2C_CONSET);
			writel(CONCLR_SIC, i2c->regs + I2C_CONCLR);

			ret = lpc40xx_i2c_busy(i2c);
			if (ret)
				return ret;

			writel(CONCLR_AAC, i2c->regs + I2C_CONCLR);

			status = lpc40xx_i2c_status(i2c);
//pr_info("F Addr:0x%02x Status:0x%02x Data:0x%02x\n", msg->addr, status, readl(i2c->regs + I2C_DAT));
			if (status != (last ? STAT_DATAR_NACK : STAT_DATAR_ACK))
				return -EIO;
			
			msg->buf[i] = readl(i2c->regs + I2C_DAT);
		} else {
			writel(msg->buf[i], i2c->regs + I2C_DAT);
			writel(CONCLR_SIC, i2c->regs + I2C_CONCLR);

			ret = lpc40xx_i2c_busy(i2c);
			if (ret)
				return ret;

			status = lpc40xx_i2c_status(i2c);
//pr_info("W Addr:0x%02x Status:0x%02x Data:0x%02x\n", msg->addr, status, readl(i2c->regs + I2C_DAT));
			if (status != STAT_DATAW_ACK)
				return -EIO;
		}
	}

	return 0;
}

static int lpc40xx_i2c_xfer(struct i2c_adapter *adapter, struct i2c_msg *msgs,
			    int num)
{
	struct lpc40xx_i2c *i2c =
		container_of(adapter, struct lpc40xx_i2c, adapter);
	int ret, i;

	ret = lpc40xx_i2c_start(i2c);
	if (!ret) {
		i2c->repstart = -1;
		for (i = 0; i < num; i++) {
			ret = lpc40xx_i2c_xfer_one(i2c, &msgs[i]);
			if (ret)
				break;
		}
	}

	lpc40xx_i2c_stop(i2c);

	return (ret < 0) ? ret : num;
}

static void __init lpc40xx_i2c_set_clk(struct lpc40xx_i2c *i2c, int rate)
{
	unsigned long pclk = clk_get_rate(i2c->clk);
	u32 duty = DIV_ROUND_UP(pclk, 2 * rate);

	if (duty < 0x0004)
		duty = 0x0004;
	else if (duty > 0xffff)
		duty = 0xffff;

	writel(duty, i2c->regs + I2C_SCLH);
	writel(duty, i2c->regs + I2C_SCLL);
}

static int __init lpc40xx_i2c_probe(struct device_d *dev)
{
	struct i2c_platform_data *pdata;
	struct lpc40xx_i2c *i2c;
	int ret;

	i2c = xzalloc(sizeof(*i2c));
	dev->priv = i2c;

	i2c->regs = dev_request_mem_region(dev, 0);
	if (IS_ERR(i2c->regs)) {
		ret = PTR_ERR(i2c->regs);
		goto out_err;
	}

	i2c->clk = clk_get(dev, NULL);
	if (IS_ERR(i2c->clk)) {
		ret = PTR_ERR(i2c->clk);
		goto out_err;
	}

	i2c->adapter.master_xfer = lpc40xx_i2c_xfer;
	i2c->adapter.dev.parent = dev;
	i2c->adapter.dev.device_node = dev->device_node;
	i2c->adapter.nr = dev->id;

	pdata = dev->platform_data;

	/* Disable I2C module */
	writel(CONCLR_AAC | CONCLR_SIC | CONCLR_STAC | CONCLR_I2ENC,
	       i2c->regs + I2C_CONCLR);
	/* Disable MONITOR mode */
	writel(0, i2c->regs + I2C_MMCTRL);

	/* Setup clock */
	if (pdata && (pdata->bitrate > 0))
		lpc40xx_i2c_set_clk(i2c, pdata->bitrate);
	else
		lpc40xx_i2c_set_clk(i2c, 100000);

	ret = i2c_add_numbered_adapter(&i2c->adapter);
	if (!ret)
		return 0;

	clk_put(i2c->clk);

out_err:
	free(i2c);

	return ret;
}

static struct driver_d lpc40xx_i2c_driver = {
	.name 	= "lpc40xx-i2c",
	.probe	= lpc40xx_i2c_probe,
};
device_platform_driver(lpc40xx_i2c_driver);
