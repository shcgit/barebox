/*
 * Copyright (C) 2014 Alexander Shiyan <shc_work@mail.ru>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 */

#include <common.h>
#include <init.h>
#include <malloc.h>
#include <linux/err.h>

#include <linux/basic_mmio_gpio.h>

#define GPIO_DIR	(0x00)
#define GPIO_MASK	(0x10)
#define GPIO_PIN	(0x14)
#define GPIO_SET	(0x18)
#define GPIO_CLR	(0x1c)

static int lpc40xx_gpio_probe(struct device_d *dev)
{
	void __iomem *base;
	struct bgpio_chip *bgc;
	int err;

	if (dev->id < 0 || dev->id > 5)
		return -ENODEV;

	base = dev_request_mem_region(dev, 0);
	if (IS_ERR(base))
		return PTR_ERR(base);

	bgc = xzalloc(sizeof(struct bgpio_chip));
	if (!bgc)
		return -ENOMEM;

	writel(0, base + GPIO_MASK);

	err = bgpio_init(bgc, dev, 4, base + GPIO_PIN, base + GPIO_SET,
			 base + GPIO_CLR, base + GPIO_DIR, NULL, 0);
	if (err)
		goto out_err;

	bgc->gc.base = dev->id * 32;
	bgc->gc.ngpio = (dev->id < 5) ? 32 : 5;

	err = gpiochip_add(&bgc->gc);

out_err:
	if (err)
		free(bgc);

	return err;
}

static struct driver_d lpc40xx_gpio_driver = {
	.name	= "lpc40xx-gpio",
	.probe	= lpc40xx_gpio_probe,
};

static __init int lpc40xx_gpio_register(void)
{
	return platform_driver_register(&lpc40xx_gpio_driver);
}
coredevice_initcall(lpc40xx_gpio_register);
