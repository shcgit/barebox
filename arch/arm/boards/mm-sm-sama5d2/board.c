// SPDX-License-Identifier: GPL-2.0-only

#include <bbu.h>
#include <bootsource.h>
#include <common.h>
#include <envfs.h>
#include <init.h>
#include <of.h>
#include <machine_id.h>
#include <net.h>
#include <linux/nvmem-consumer.h>

static void __init *nvmem_read(struct device_node *np, const char *cell_name,
			       size_t *bytes)
{
	struct nvmem_cell *cell;
	void *value;
	size_t len;

	cell = of_nvmem_cell_get(np, cell_name);
	if (IS_ERR(cell))
		return cell;

	value = nvmem_cell_read(cell, &len);
	if (!IS_ERR(value))
		*bytes = len;

	nvmem_cell_put(cell);

	return value;
}

static int __init mm_sm_sama5d2_serial_init(void)
{
	struct device_node *np;
	size_t len = 0;
	char *serial;
	u32 *ser32;
	void *buf;

	if (!of_machine_is_compatible("milas,mm-sm-sama5d2"))
		return 0;

	np = of_find_node_by_name_address(NULL, "device-info");
	if (!np) {
		pr_err("Cannot get device-info node!\n");
		return -ENODEV;
	}

	buf = nvmem_read(np, "serial", &len);
	if (IS_ERR(buf) || len < 16) {
		pr_err("Cannot read EEPROM!\n");
		return PTR_ERR(buf);
	};

	machine_id_set_hashable(buf, len);
	ser32 = (u32 *)buf;
	serial = basprintf("%08x%08x", ser32[0], ser32[1]);
	kfree(buf);
	barebox_set_serial_number(serial);
	free(serial);

	return 0;
}
of_populate_initcall(mm_sm_sama5d2_serial_init);

static int __init mm_sm_sama5d2_mac_init(void)
{
	struct device_node *np;
	u8 ethaddr[ETH_ALEN];
	int ret;

	if (!of_machine_is_compatible("milas,mm-sm-sama5d2"))
		return 0;

	ret = generate_ether_addr(ethaddr, 0);
	if (ret) {
		pr_err("Cannot generate ethernet address!\n");
		return ret;
	}

	np = of_find_node_by_alias(of_get_root_node(), "ethernet0");
	if (!np) {
		pr_err("Cannot get ethernet node!\n");
		return -ENODEV;
	}

	of_eth_register_ethaddr(np, ethaddr);

	return 0;
}
environment_initcall(mm_sm_sama5d2_mac_init);

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
			pr_warn("Unhandled boot source: %s, instance %i\n",
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
