/*
 * Copyright (C) 2014 Alexander Shiyan <shc_work@mail.ru>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 */

#include <errno.h>
#include <clock.h>
#include <common.h>
#include <gpio.h>
#include <init.h>
#include <io.h>
#include <malloc.h>
#include <linux/clk.h>
#include <spi/spi.h>

#define CR0	(0x00)
# define DSS(x)	((x) - 1)
# define CPOL	BIT(6)
# define CPHA	BIT(7)
# define SCR(x)	((x) << 8)
#define CR1	(0x04)
# define LBM	BIT(0)
# define SSE	BIT(1)
#define DR	(0x08)
#define SR	(0x0c)
# define BSY	BIT(4)
#define CPSR	(0x10)

struct lpc40xx_spi {
	struct spi_master	master;
	struct clk		*clk;
	void __iomem		*regs;
};

static int lpc40xx_chipselect(struct spi_device *spi, bool active)
{
	int *gpios = spi->master->dev->platform_data;
	int gpio = gpios[spi->chip_select];

	gpio_set_value(gpio, active ^ !(spi->mode & SPI_CS_HIGH));

	return 0;
}

static int lpc40xx_spi_setup(struct spi_device *spi)
{
	unsigned long gpiof = GPIOF_OUT_INIT_HIGH;
	int *gpios = spi->master->dev->platform_data;
	int gpio = gpios[spi->chip_select];

	if ((spi->bits_per_word < 4) || (spi->bits_per_word > 16))
		return -EINVAL;

	if (spi->mode & (SPI_LSB_FIRST | SPI_3WIRE))
		return -EINVAL;

	if (spi->mode & SPI_CS_HIGH)
		gpiof = GPIOF_OUT_INIT_LOW;

	return gpio_request_one(gpio, gpiof, dev_name(spi->master->dev));
}

static int lpc40xx_spi_transfer(struct spi_device *spi, struct spi_message *msg)
{
	struct spi_master *master = spi->master;
	struct lpc40xx_spi *lpc40xx_spi =
		container_of(master, struct lpc40xx_spi, master);
	unsigned long rate = clk_get_rate(lpc40xx_spi->clk);
	bool speed_valid = false;
	struct spi_transfer *t;
	u32 cr0 = 0, cr1 = 0;
	int ret = 0;

	if (spi->mode & SPI_CPHA)
		cr0 |= CPHA;
	if (spi->mode & SPI_CPOL)
		cr0 |= CPOL;
	if (spi->mode & SPI_LOOP)
		cr1 |= LBM;

	msg->actual_length = 0;

	list_for_each_entry(t, &msg->transfers, transfer_list) {
		u8 bpw = t->bits_per_word ? : spi->bits_per_word;
		u32 speed = t->speed_hz ? : spi->max_speed_hz;
		const u8 *txdata = t->tx_buf;
		u32 i, div = 0, scr, cpsr;
		u8 *rxdata = t->rx_buf;

		lpc40xx_chipselect(spi, true);

		if (speed > 0) {
			div = DIV_ROUND_UP(rate, speed);
			if (div % 2)
				div++;
		}
		if (div < 2)
			div = 2;

		for (cpsr = 2; cpsr <= 254; cpsr += 2) {
			scr = div / cpsr;
			if (scr < 1) {
				scr = 1;
				speed_valid = true;
				break;
			}
			if (scr <= 256) {
				speed_valid = true;
				break;
			}
		}

		if (!speed_valid) {
			ret = -ERANGE;
			goto xfer_out;
		}

		writel(cr1, lpc40xx_spi->regs + CR1);
		writel(cr0 | DSS(bpw) | SCR(scr - 1), lpc40xx_spi->regs + CR0);
		writel(cpsr, lpc40xx_spi->regs + CPSR);
		writel(cr1 | SSE, lpc40xx_spi->regs + CR1);

		for (i = 0; i < t->len; i += DIV_ROUND_UP(bpw, 8)) {
			u32 data = 0xffff;

			if (txdata) {
				data = *txdata++;
				if (bpw > 8)
					data |= (*txdata++ << 8);
			}
			writel(data, lpc40xx_spi->regs + DR);

			if (wait_on_timeout(10 * MSECOND,
					    !(readl(lpc40xx_spi->regs + SR) & BSY))) {
				ret = -ETIMEDOUT;
				goto xfer_out;
			}


			data = readl(lpc40xx_spi->regs + DR);
			if (rxdata) {
				*rxdata++ = (data & 0xff);
				if (bpw > 8)
					*rxdata++ = (data >> 8) & 0xff;
			}
		}

		if (t->cs_change)
			lpc40xx_chipselect(spi, false);

		msg->actual_length += t->len;
	}

xfer_out:
	writel(0, lpc40xx_spi->regs + CR1);
	lpc40xx_chipselect(spi, false);

	return ret;
}

static int lpc40xx_spi_probe(struct device_d *dev)
{
	struct lpc40xx_spi *lpc40xx_spi;
	struct spi_master *master;
	int i, err, *gpios;

	gpios = dev->platform_data;
	if (!gpios)
		return -EINVAL;

	lpc40xx_spi = xzalloc(sizeof(*lpc40xx_spi));

	master = &lpc40xx_spi->master;
	master->dev = dev;
	master->bus_num = dev->id;
	master->setup = lpc40xx_spi_setup;
	master->transfer = lpc40xx_spi_transfer;

	for (i = 0; gpio_is_valid(gpios[i]); i++)
		master->num_chipselect++;

	lpc40xx_spi->regs = dev_request_mem_region(dev, 0);
	if (IS_ERR(lpc40xx_spi->regs)) {
		err = PTR_ERR(lpc40xx_spi->regs);
		goto out_err;
	}

	lpc40xx_spi->clk = clk_get(dev, NULL);
	if (IS_ERR(lpc40xx_spi->clk)) {
		err = PTR_ERR(lpc40xx_spi->clk);
		goto out_err;
	}

	err = clk_enable(lpc40xx_spi->clk);
	if (err) {
		clk_put(lpc40xx_spi->clk);
		goto out_err;
	}

	err = spi_register_master(master);
	if (!err)
		return 0;

	clk_disable(lpc40xx_spi->clk);

out_err:
	if (err)
		free(lpc40xx_spi);

	return err;
}

static struct driver_d lpc40xx_spi_driver = {
	.name  = "lpc40xx-spi",
	.probe = lpc40xx_spi_probe,
};
device_platform_driver(lpc40xx_spi_driver);
