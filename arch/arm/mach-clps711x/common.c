// SPDX-License-Identifier: GPL-2.0+
// Author: Alexander Shiyan <shc_work@mail.ru>

#include <driver.h>
#include <envfs.h>
#include <init.h>
#include <of.h>
#include <restart.h>

#include <asm/io.h>
#include <asm/mmu.h>

#include "clps711x.h"

#define CLPS711X_MAP_ADDR	0x90000000

static u32 remap_size = 0;

static void __noreturn clps711x_restart(struct restart_handler *rst)
{
	shutdown_barebox();

	asm("mov pc, #0");

	hang();
}

static int clps711x_dt_fixup(struct device_node *root, void *context)
{
	char *serial = basprintf("%08x%08x", 0, readl(UNIQID));

	of_set_property(root, "serial-number", serial, strlen(serial) + 1, 1);

	free(serial);

	return 0;
}

static __init int clps711x_fixup(void)
{
	if (of_machine_is_compatible("cirrus,ep7209")) {
		restart_handler_register_fn(clps711x_restart);
		of_register_fixup(clps711x_dt_fixup, NULL);
	}

	return 0;
}
postcore_initcall(clps711x_fixup);

static __init int clps711x_defaultenv_init(void)
{
	if (of_machine_is_compatible("cirrus,ep7209"))
		defaultenv_append_directory(defaultenv_clps711x);

	return 0;
}
device_initcall(clps711x_defaultenv_init);

static int __init clps711x_bus_map(void)
{
	if (of_machine_is_compatible("cirrus,ep7209") && remap_size)
		map_io_sections(0, (void *)CLPS711X_MAP_ADDR, remap_size);

	return 0;
}
postmmu_initcall(clps711x_bus_map);

static void clps711x_bus_patch(struct device_node *node,
			       u32 compare, u32 change)
{
	const __be32 *ranges;
	int rsize;

	ranges = of_get_property(node, "ranges", &rsize);

	if (ranges) {
		int banks = rsize / (sizeof(u32) * 4);
		__be32 *fixed, *fixedptr;

		fixed = xmalloc(rsize);
		fixedptr = fixed;

		while (banks--) {
			u32 bank, cell, addr, size;

			bank = be32_to_cpu(*ranges++);
			cell = be32_to_cpu(*ranges++);
			addr = be32_to_cpu(*ranges++);
			size = be32_to_cpu(*ranges++);

			if (addr == compare) {
				addr = change;
				remap_size = size;
			}

			*fixedptr++ = cpu_to_be32(bank);
			*fixedptr++ = cpu_to_be32(cell);
			*fixedptr++ = cpu_to_be32(addr);
			*fixedptr++ = cpu_to_be32(size);
		}

		of_set_property(node, "ranges", fixed, rsize, 0);

		free(fixed);
	}
}

static int clps711x_bus_fixup(struct device_node *root, void *context)
{
	struct device_node *node = context;

	/* Remove patch before boot */
	if (remap_size)
		clps711x_bus_patch(node, CLPS711X_MAP_ADDR, 0);

	return 0;
}

static int clps711x_bus_probe(struct device_d *dev)
{
	u32 mcfg;

	/* Setup bus timings */
	if (!of_property_read_u32(dev->device_node,
				  "barebox,ep7209-memcfg1", &mcfg))
		writel(mcfg, MEMCFG1);
	if (!of_property_read_u32(dev->device_node,
				  "barebox,ep7209-memcfg2", &mcfg))
		writel(mcfg, MEMCFG2);

	/* Patch bus for zero address */
	clps711x_bus_patch(dev->device_node, 0, CLPS711X_MAP_ADDR);

	of_platform_populate(dev->device_node, NULL, dev);

	of_register_fixup(clps711x_bus_fixup, dev->device_node);

	return 0;
}

static struct of_device_id __maybe_unused clps711x_bus_dt_ids[] = {
	{ .compatible = "cirrus,ep7209-bus", },
	{ }
};

static struct driver_d clps711x_bus_driver = {
	.name = "clps711x-bus",
	.probe = clps711x_bus_probe,
	.of_compatible = DRV_OF_COMPAT(clps711x_bus_dt_ids),
};

static int __init clps711x_bus_init(void)
{
	return platform_driver_register(&clps711x_bus_driver);
}
core_initcall(clps711x_bus_init);
