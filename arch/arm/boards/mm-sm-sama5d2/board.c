/* SPDX-License-Identifier: GPL-2.0+ */
/* SPDX-FileCopyrightText: Alexander Shiyan <shc_work@mail.ru> */

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
#include <i2c/i2c.h>
#include <linux/nvmem-consumer.h>

static int __init bootcfg = -1;
static int __init somrev = -1;
static int __init evbrev = -1;
static int __init infrev = -1;

static int __init i2c_probe(struct i2c_adapter *adapter, const int addr)
{
	u8 buf[1];
	struct i2c_msg msg = {
		.addr = addr,
		.buf = buf,
		.len = sizeof(buf),
		.flags = I2C_M_RD,
	};

	return (i2c_transfer(adapter, &msg, 1) == 1) ? 0: -ENODEV;
}

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
		return IS_ERR(buf) ? PTR_ERR(buf) : -ETOOSMALL;
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

struct gpioset {
	unsigned id;
	unsigned shift;
};

static const struct gpioset __init bootgpios[] = {
	{ 1, 0, },
	{ 2, 1, },
	{ 3, 2, },
};

static const struct gpioset __init vergpios[] = {
	{ 4, 0, },
	{ 5, 1, },
	{ 6, 2, },
	{ 7, 3, },
};

static int __init mm_sm_sama5d2_gpio_get(const unsigned gpio)
{
	int ret;

	ret = gpio_direction_input(gpio);
	if (ret)
		return ret;

	return gpio_get_value(gpio);
}

static int __init mm_sm_sama5d2_gpios_get(const char *name,
					  const struct gpioset *gpioset,
					  int count)
{
	struct device *gpiodev;
	struct gpio_chip *gpiochip;
	int i, val, ret = 0;

	gpiodev = get_device_by_name(name);
	if (!gpiodev)
		return -ENODEV;

	gpiochip = gpio_get_chip_by_dev(gpiodev);
	if (!gpiochip)
		return -ENODEV;

	for (i = 0; i < count; i++) {
		val = mm_sm_sama5d2_gpio_get(gpiochip->base + gpioset[i].id);
		if (val < 0) {
			pr_err("Cannot get GPIO %u\n",
			       gpiochip->base + gpioset[i].id);
			return val;
		}
		ret |= val << gpioset[i].shift;
	}

	return ret;
}

static int __init mm_sm_sama5d2_version_init(void)
{
	const char *piobu = "fc040000.secumod@fc040000.of";
	int ret;

	ret = mm_sm_sama5d2_gpios_get(piobu, bootgpios, ARRAY_SIZE(bootgpios));
	if (ret < 0)
		return ret;

	bootcfg = ret;

	ret = mm_sm_sama5d2_gpios_get(piobu, vergpios, ARRAY_SIZE(vergpios));
	if (ret < 0)
		return ret;

	somrev = ret;

	return 0;
}

static int __init mm_sm_sama5d2_evb_init(void)
{
	struct device *dev;
	struct i2c_adapter *adapter;

	dev = of_device_enable_and_register_by_alias("i2c0");
	if (!dev)
		return -ENODEV;

	adapter = i2c_get_adapter(0);
	if (!adapter)
		return -ENODEV;

	if (i2c_probe(adapter, 0x24)) {
		//TODO:
		//return -ENODEV;
	}

	//TODO:
	evbrev = 0;

	return 0;
}

static int __init mm_sm_sama5d2_informer_init(void)
{
	struct device *dev;
	struct i2c_adapter *adapter;

	dev = of_device_enable_and_register_by_alias("i2c0");
	if (!dev)
		return -ENODEV;

	adapter = i2c_get_adapter(0);
	if (!adapter)
		return -ENODEV;

	if (i2c_probe(adapter, 0x20)) {
		//TODO:
		//return -ENODEV;
	}

	//TODO:

	return -ENOTSUPP;
}

static int __init mm_sm_sama5d2_load_overlay(const void *ovl)
{
	int ret;

	if (ovl) {
		struct device_node *root = of_get_root_node();

		ret = of_overlay_apply_dtbo(root, ovl);
		if (ret) {
			pr_err("Cannot apply overlay: %pe!\n", ERR_PTR(ret));
			return ret;
		}

		of_probe();

		/* Ensure reload aliases & model name */
		of_set_root_node(NULL);
		of_set_root_node(root);
	}

	return 0;
}

static int __init mm_sm_sama5d2_init(void)
{
	const void *som_ovl = NULL, *brd_ovl = NULL, *ver_ovl = NULL;
	extern char __dtbo_mm_sm_sama5d2_evb_start[];
	extern char __dtbo_mm_sm_sama5d2_informer_start[];
	enum bootsource bootsource;
	int instance, ret;

	if (!of_machine_is_compatible("milas,mm-sm-sama5d2"))
		return 0;

	if (mm_sm_sama5d2_version_init()) {
		pr_err("Cannot determine module version!\n");
		return -ENOTSUPP;
	}

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

	ret = mm_sm_sama5d2_load_overlay(som_ovl);
	if (ret)
		return ret;

	if (mm_sm_sama5d2_evb_init() && mm_sm_sama5d2_informer_init()) {
		pr_err("Cannot determine board variant!\n");
		return -ENOTSUPP;
	}

	if (evbrev > -1) {
		pr_info("EVB revision: %i\n", evbrev);
		brd_ovl = __dtbo_mm_sm_sama5d2_evb_start;
	} else if (infrev > -1) {
		pr_info("Informer revision: %i\n", infrev);
		brd_ovl = __dtbo_mm_sm_sama5d2_informer_start;
	} else {
		pr_err("Unsupported hardware\n");
		hang();
	}

	ret = mm_sm_sama5d2_load_overlay(brd_ovl);
	if (ret)
		return ret;

	ret = mm_sm_sama5d2_load_overlay(ver_ovl);
	if (ret)
		return ret;

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
