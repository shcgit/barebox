// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: Alexander Shiyan <shc_work@mail.ru>

#include <envfs.h>
#include <init.h>
#include <of.h>
#include <stdio.h>
#include <asm/io.h>
#include <mach/clps711x.h>

static int clep7212_dt_fixup(struct device_node *root, void *context)
{
	char *serial = basprintf("%08x%08x", 0, readl(UNIQID));

	of_set_property(root, "serial-number", serial, strlen(serial) + 1, 1);

	free(serial);

	return 0;
}

static __init int clep7212_fixup(void)
{
	if (of_machine_is_compatible("cirrus,ep7209"))
		of_register_fixup(clep7212_dt_fixup, NULL);

	return 0;
}
postcore_initcall(clep7212_fixup);

static __init int clep7212_defaultenv_init(void)
{
	if (of_machine_is_compatible("cirrus,ep7209"))
		defaultenv_append_directory(defaultenv_clep7212);

	return 0;
}
device_initcall(clep7212_defaultenv_init);
