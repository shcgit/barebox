// SPDX-License-Identifier: GPL-2.0-only

#include <init.h>
#include <envfs.h>
//#include <bbu.h>
#include <of.h>

static int mm_sm_sama5d2_init(void)
{
	if (!of_machine_is_compatible("milas,mm-sm-sama5d2"))
		return 0;

//	bbu_register_std_file_update("microSD", BBU_HANDLER_FLAG_DEFAULT,
//				     "/mnt/mmc1.0/barebox.bin",
//				     filetype_arm_barebox);

	defaultenv_append_directory(defaultenv_mm_sm_sama5d2);

	return 0;
}
device_initcall(mm_sm_sama5d2_init);
