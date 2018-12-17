/*
 * Copyright (C) 2012 Alexander Shiyan <shc_work@mail.ru>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 */

#include <common.h>
#include <init.h>
#include <linux/sizes.h>
#include <of.h>

#include <asm/io.h>
#include <asm/memory.h>

#include <mach/clps711x.h>

static int clps711x_bus_fixup(struct device_node *root, void *context)
{
	struct device_node *bus_node = context;

	pr_info("clps711x-bus fixup %p\n", bus_node);
	//
	return 0;
}

static int clps711x_fixup(void)
{
	struct device_node *bus_node;

//of_machine_is_compatible
	pr_info("compat %i\n", of_machine_is_compatible("cirrus,ep7209") ? 1 : 0);
//	"cirrus,ep7209-bus"
	bus_node = of_find_compatible_node(NULL, NULL, "cirrus,ep7209-bus");
	if (bus_node) {
	const void *ranges;
	int na, ns, rlen;

		pr_info("found %p\n", bus_node);

	ranges = of_get_property(bus_node, "ranges", &rlen);
	if (ranges) {
		u64 offset, size;

		na = of_n_addr_cells(bus_node);
		ns = of_n_size_cells(bus_node);
pr_info("na %i ns %i\n",na,ns);
		offset = of_read_number(ranges, na);
		size = of_read_number(ranges + na, ns);
pr_info("offset %llu size %llu\n",offset,size);
	}

//		of_print_nodes
		of_register_fixup(clps711x_bus_fixup, bus_node);
	} else
		pr_info("not found\n");

	return 0;
}
postcore_initcall(clps711x_fixup);

static int clps711x_mem_init(void)
{
//	ulong memsize = get_ram_size((ulong *)SDRAM0_BASE, SZ_64M);
//
	arm_add_mem_device("ram0", SDRAM0_BASE, SZ_8M);

	return 0;
}
mem_initcall(clps711x_mem_init);

inline static void _clps711x_setup_memcfg(int bank, u32 addr, u32 val)
{
	u32 tmp = readl(addr);

	tmp &= ~(0xff << (bank * 8));
	tmp |= val << (bank * 8);

	writel(tmp, addr);
}

void clps711x_setup_memcfg(int bank, u32 val)
{
	switch (bank) {
	case 0 ... 3:
		_clps711x_setup_memcfg(bank, MEMCFG1, val);
		break;
	case 4 ... 5:
		_clps711x_setup_memcfg(bank - 4, MEMCFG2, val);
		break;
	}
}

static struct resource uart0_resources[] = {
	DEFINE_RES_MEM(UARTDR1, SZ_128),
};

static struct resource uart1_resources[] = {
	DEFINE_RES_MEM(UARTDR2, SZ_128),
};

void clps711x_add_uart(unsigned int id)
{
	switch (id) {
	case 0:
		add_generic_device_res("clps711x-uart", 0, uart0_resources,
				       ARRAY_SIZE(uart0_resources), NULL);
		break;
	case 1:
		add_generic_device_res("clps711x-uart", 1, uart1_resources,
				       ARRAY_SIZE(uart1_resources), NULL);
		break;
	}
}

static struct resource gpio0_resources[] = {
	DEFINE_RES_MEM(PADR, SZ_1),
	DEFINE_RES_MEM(PADDR, SZ_1),
};

static struct resource gpio1_resources[] = {
	DEFINE_RES_MEM(PBDR, SZ_1),
	DEFINE_RES_MEM(PBDDR, SZ_1),
};

static struct resource gpio2_resources[] = {
	DEFINE_RES_MEM(PCDR, SZ_1),
	DEFINE_RES_MEM(PCDDR, SZ_1),
};

static struct resource gpio3_resources[] = {
	DEFINE_RES_MEM(PDDR, SZ_1),
	DEFINE_RES_MEM(PDDDR, SZ_1),
};

static struct resource gpio4_resources[] = {
	DEFINE_RES_MEM(PEDR, SZ_1),
	DEFINE_RES_MEM(PEDDR, SZ_1),
};

static __init int clps711x_gpio_init(void)
{
	add_generic_device_res("clps711x-gpio", 0, gpio0_resources,
			       ARRAY_SIZE(gpio0_resources), NULL);
	add_generic_device_res("clps711x-gpio", 1, gpio1_resources,
			       ARRAY_SIZE(gpio1_resources), NULL);
	add_generic_device_res("clps711x-gpio", 2, gpio2_resources,
			       ARRAY_SIZE(gpio2_resources), NULL);
	add_generic_device_res("clps711x-gpio", 3, gpio3_resources,
			       ARRAY_SIZE(gpio3_resources), NULL);
	add_generic_device_res("clps711x-gpio", 4, gpio4_resources,
			       ARRAY_SIZE(gpio4_resources), NULL);

	return 0;
}
coredevice_initcall(clps711x_gpio_init);

//static int clps711x_bus_fixup(struct device_node *root, void *unused)
//{
//	pr_info("clps711x-bus fixup\n");
	//
//	return 0;
//}

static int clps711x_bus_probe(struct device_d *dev)
{
	const void *ranges;
	int na, ns, rlen;

	pr_info("clps711x-bus probe\n");

	ranges = of_get_property(dev->device_node, "ranges", &rlen);
	if (ranges) {
		u64 offset, size;

		na = of_n_addr_cells(dev->device_node);
		ns = of_n_size_cells(dev->device_node);
pr_info("na %i ns %i\n",na,ns);
		offset = of_read_number(ranges, na);
		size = of_read_number(ranges + na, ns);
pr_info("offset %llu size %llu\n",offset,size);
	}

	//
//	of_register_fixup(clps711x_bus_fixup, NULL);
	//
	return 0;
}

static const struct of_device_id clps711x_bus_dt_ids[] = {
	{ .compatible = "cirrus,ep7209-bus", },
	{ }
};

static struct driver_d clps711x_bus_driver = {
	.name = "clps711x-bus",
	.probe = clps711x_bus_probe,
	.of_compatible = DRV_OF_COMPAT(clps711x_bus_dt_ids),
};

static __init int clps711x_system_init(void)
{
	/* SYSCON1, SYSFLG1 */
	add_generic_device("syscon", 1, NULL, SYSCON1, SZ_128,
			   IORESOURCE_MEM, NULL);
	/* SYSCON2, SYSFLG2 */
	add_generic_device("syscon", 2, NULL, SYSCON2, SZ_128,
			   IORESOURCE_MEM, NULL);

	return 0;//platform_driver_register(&clps711x_bus_driver);
}
postcore_initcall(clps711x_system_init);
