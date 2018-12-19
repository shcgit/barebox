// SPDX-License-Identifier: GPL-2.0+
// Author: Alexander Shiyan <shc_work@mail.ru>

#include <envfs.h>
#include <init.h>

static int clps711x_devices_init(void)
{
//if (of_machine_is_compatible("cirrus,ep7209"))
	defaultenv_append_directory(defaultenv_clep7212);

	return 0;
}
device_initcall(clps711x_devices_init);
