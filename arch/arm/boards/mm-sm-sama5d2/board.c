// SPDX-License-Identifier: GPL-2.0-only

#include <bbu.h>
#include <bootsource.h>
#include <common.h>
#include <envfs.h>
#include <init.h>
#include <of.h>

static int __init mm_sm_sama5d2_init(void)
{
	enum bootsource bootsource;
	int instance;

	if (!of_machine_is_compatible("milas,mm-sm-sama5d2"))
		return 0;

	bootsource = bootsource_get();
	instance = bootsource_get_instance();

	if (bootsource != BOOTSOURCE_MMC || !instance) {
		if (bootsource != BOOTSOURCE_SPI) {
			pr_warn("Boot source: %s, instance %i\n",
				bootsource_to_string(bootsource), instance);
		} else
			of_device_enable_path("/chosen/environment-qspi");
	} else
		of_device_enable_path("/chosen/environment-sd");

	bbu_register_std_file_update("sd", BBU_HANDLER_FLAG_DEFAULT,
				     "/mnt/mmc1.0/barebox.bin",
				     filetype_arm_barebox);

	defaultenv_append_directory(defaultenv_mm_sm_sama5d2);

	return 0;
}
device_initcall(mm_sm_sama5d2_init);
