// SPDX-License-Identifier: GPL-2.0-only

#include <bbu.h>
#include <bootsource.h>
#include <common.h>
#include <driver.h>
#include <envfs.h>
#include <gpio.h>
#include <init.h>
#include <of.h>
#include <machine_id.h>
#include <net.h>
#include <linux/nvmem-consumer.h>

static int __init bootcfg = -1;
static int __init somrev = -1;

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

static int __init mm_sm_sama5d2_gpio_get(const unsigned gpio)
{
	int ret;

	ret = gpio_direction_input(gpio);
	if (ret)
		return ret;

	return gpio_get_value(gpio);
}

static int __init mm_sm_sama5d2_version_init(void)
{
	const struct {
		unsigned id;
		unsigned shift;
	} bootgpios[] = {
		{ 1, 0, },
		{ 2, 1, },
		{ 3, 2, },
	}, vergpios[] = {
		{ 4, 0, },
		{ 5, 1, },
		{ 6, 2, },
		{ 7, 3, },
	};
	struct device *gpiodev;
	struct gpio_chip *gpiochip;
	int i, val, ret;

	gpiodev = get_device_by_name("fc040000.secumod@fc040000.of");
	if (!gpiodev)
		return -ENODEV;

	gpiochip = gpio_get_chip_by_dev(gpiodev);
	if (!gpiochip)
		return -ENODEV;

	ret = 0;
	for (i = 0; i < ARRAY_SIZE(bootgpios); i++) {
		val = mm_sm_sama5d2_gpio_get(gpiochip->base + bootgpios[i].id);
		if (val < 0) {
			pr_err("Cannot get PIOBU %u\n", bootgpios[i].id);
			return val;
		}
		ret |= val << bootgpios[i].shift;
	}
	bootcfg = ret;

	ret = 0;
	for (i = 0; i < ARRAY_SIZE(vergpios); i++) {
		val = mm_sm_sama5d2_gpio_get(gpiochip->base + vergpios[i].id);
		if (val < 0) {
			pr_err("Cannot get PIOBU %u\n", vergpios[i].id);
			return val;
		}
		ret |= val << vergpios[i].shift;
	}
	somrev = ret;

	return 0;
}

static int __init mm_sm_sama5d2_init(void)
{
	enum bootsource bootsource;
	int instance;

	if (!of_machine_is_compatible("milas,mm-sm-sama5d2"))
		return 0;

	if (mm_sm_sama5d2_version_init()) {
		pr_err("Cannot determine module version.\n");
		return -ENOTSUPP;
	}

	bootsource = bootsource_get();
	instance = bootsource_get_instance();

	switch (bootcfg) {
	case 0 ... 7:
		break;
	default:
		pr_err("Unhandled SMARC BOOT settings!\n");
		return -ENOTSUPP;
	}

	pr_info("SMARC BOOT settings: %i (%d%d%d)\n", bootcfg, !!(bootcfg & 0x4),
							       !!(bootcfg & 0x2),
							       !!(bootcfg & 0x1));

	switch (somrev) {
	case 0:
		break;
	default:
		pr_err("Unhandled module revision!\n");
		return -ENOTSUPP;
	}

	pr_info("Module revision: %i\n", somrev);

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
