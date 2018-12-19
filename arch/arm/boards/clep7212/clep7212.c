/*
 * Copyright (C) 2012 Alexander Shiyan <shc_work@mail.ru>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 */

//#include <common.h>
#include <envfs.h>
#include <init.h>

static int clps711x_devices_init(void)
{
//if (of_machine_is_compatible("cirrus,ep7209"))
	defaultenv_append_directory(defaultenv_clep7212);

	return 0;
}
device_initcall(clps711x_devices_init);
