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
#include <mach/at91/sama5d2.h>

static void __init *som = NULL;
static void __init *board = NULL;
static void __init *patch = NULL;

static int __init i2c_probe(struct i2c_adapter *adapter, const int addr)
{
	u8 buf[1];
	struct i2c_client client;

	client.adapter = adapter;
	client.addr = addr;

	return (i2c_read_reg(&client, 0, buf, sizeof(buf)) == sizeof(buf)) ? 0 : -ENODEV;
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

static int __init mm_sm_sama5d2_som_init(void)
{
	const char *piobu = "fc040000.secumod@fc040000.of";
	int ret;

	ret = mm_sm_sama5d2_gpios_get(piobu, bootgpios, ARRAY_SIZE(bootgpios));
	if (ret < 0)
		return ret;

	switch (ret) {
	case 0 ... 7:
		break;
	default:
		pr_err("Unhandled SMARC BOOT settings!\n");
		return -ENOTSUPP;
	}

	pr_info("SMARC BOOT settings: %i (%d%d%d)\n", ret, !!(ret & 0x4),
							   !!(ret & 0x2),
							   !!(ret & 0x1));

	ret = mm_sm_sama5d2_gpios_get(piobu, vergpios, ARRAY_SIZE(vergpios));
	if (ret < 0)
		return ret;

	switch (ret) {
	case 0:
		break;
	default:
		pr_err("Unhandled module revision!\n");
		return -ENOTSUPP;
	}

	pr_info("Module revision: %i\n", ret);

	return 0;
}

static const struct gpioset __init evbvergpios[] = {
	{ 0, 0, },
	{ 1, 1, },
	{ 2, 2, },
	{ 3, 3, },
};

static int __init mm_sm_sama5d2_patch_init(void)
{
	int ret;

	if (of_machine_is_compatible("milas,mm-sm-sama5d2-evb")) {
		ret = mm_sm_sama5d2_gpios_get("tca95340", evbvergpios, ARRAY_SIZE(evbvergpios));
		if (ret < 0)
			return ret;

		pr_info("Board revision: %i\n", ret);
		//TODO:
	} else if (of_machine_is_compatible("milas,informer-sama5d2")) {
		//TODO:
		ret = 0;
		pr_info("Board revision: %i\n", ret);
	} else
		return -ENOTSUPP;

	return 0;
}

static struct i2c_adapter __init *mm_sm_sama5d2_i2c_get_adapter(const int nr)
{
	char *alias = basprintf("i2c%i", nr);
	struct device *dev;

	dev = of_device_enable_and_register_by_alias(alias);
	free(alias);
	if (!dev)
		return NULL;

	return i2c_get_adapter(nr);
}

static int __init mm_sm_sama5d2_board_init(void)
{
	extern char __dtbo_mm_sm_sama5d2_evb_start[];
	extern char __dtbo_mm_sm_sama5d2_informer_start[];
	struct i2c_adapter *adapter;

	adapter = mm_sm_sama5d2_i2c_get_adapter(0);
	if (!adapter)
		return -ENODEV;

	if (!i2c_probe(adapter, 0x24)) {
		pr_info("Board variant detected: EVB\n");
		board = __dtbo_mm_sm_sama5d2_evb_start;

		return 0;
	} else if (1/*!i2c_probe(adapter, 0x20)*/) {
		pr_info("Board variant detected: Informer\n");
		board = __dtbo_mm_sm_sama5d2_informer_start;

		// if (bootsource_get() != BOOTSOURCE_MMC)
		// disable mmc node fixup

		return 0;
	}

	return -ENOTSUPP;
}

static int __init mm_sm_sama5d2_load_overlay(const void *ovl)
{
	if (ovl) {
		struct device_node *root = of_get_root_node();
		int ret;

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

#define SAMA5D2_SFC_KR			(SAMA5D2_BASE_SFC + 0x00)
# define SAMA5D2_SFC_KR_KEY_CODE	(0xfb)
#define SAMA5D2_SFC_SR			(SAMA5D2_BASE_SFC + 0x1c)
# define SAMA5D2_SFC_SR_PGMC		BIT(0)
# define SAMA5D2_SFC_SR_PGMF		BIT(1)

static void __init mm_sm_sama5d2_fuse(void)
{
	u32 fuse, sr, bootcfg = sama5d2_bootcfg();

	if ((FIELD_GET(SAMA5D2_BOOTCFG_EXT_MEM_BOOT_EN, bootcfg) == 1) &&
	    (FIELD_GET(SAMA5D2_BOOTCFG_QSPI_0, bootcfg) == 0))
		return;

	pr_warn("External boot is not programmed (0x%08x)!\n", bootcfg);

	if (FIELD_GET(SAMA5D2_DISABLE_BSC_CR, bootcfg) == 0) {
		u32 bsc_cr = readl(SAMA5D2_BASE_SYSC + 0x54);
		if (bsc_cr & SAMA5D2_BUREG_VALID) {
			pr_err("BUREG_VALID is set (0x%08x)!\n", bsc_cr);
			return;
		}
	}

	if (FIELD_GET(SAMA5D2_BOOTCFG_QSPI_0, bootcfg) != 0) {
		pr_err("Wrong QSPI0 configuration!\n");
		return;
	}

	fuse = FIELD_PREP(SAMA5D2_BOOTCFG_EXT_MEM_BOOT_EN, 0x1) |
	       FIELD_PREP(SAMA5D2_BOOTCFG_SPI_1, 0x3) |
	       FIELD_PREP(SAMA5D2_BOOTCFG_SDMMC_0, 0x1) |
	       FIELD_PREP(SAMA5D2_BOOTCFG_NFC, 0x3);

	writeb(SAMA5D2_SFC_KR_KEY_CODE, SAMA5D2_SFC_KR);
	writel(fuse, SAMA5D2_SFC_DR(16));

	if (readl_poll_timeout(SAMA5D2_SFC_SR, sr, sr & SAMA5D2_SFC_SR_PGMC,
			       USEC_PER_MSEC) < 0) {
		pr_err("Timeout programming fuses (0x%08x)!\n", sr);
		return;
	}

	if (sr & SAMA5D2_SFC_SR_PGMF)
		pr_err("Failure programming fuses (0x%08x)!\n", sr);

	bootcfg = sama5d2_bootcfg();

	if (FIELD_GET(SAMA5D2_BOOTCFG_EXT_MEM_BOOT_EN, bootcfg) == 1)
		pr_info("External boot successfully programmed.\n");
	else
		pr_err("External boot programming failed!\n");
}

static int __init mm_sm_sama5d2_init(void)
{
	enum bootsource bootsource;
	int instance, ret;

	if (!of_machine_is_compatible("milas,mm-sm-sama5d2"))
		return 0;

	ret = mm_sm_sama5d2_som_init();
	if (ret) {
		pr_err("Cannot determine module revision!\n");
		return ret;
	}

	ret = mm_sm_sama5d2_load_overlay(som);
	if (ret)
		return ret;

	ret = mm_sm_sama5d2_board_init();
	if (ret) {
		pr_err("Cannot determine board variant!\n");
		return ret;
	}

	ret = mm_sm_sama5d2_load_overlay(board);
	if (ret)
		return ret;

	ret = mm_sm_sama5d2_patch_init();
	if (ret) {
		pr_err("Cannot determine board revision!\n");
		return ret;
	}

	ret = mm_sm_sama5d2_load_overlay(patch);
	if (ret)
		return ret;

	bootsource = bootsource_get();
	instance = bootsource_get_instance();

	if (bootsource != BOOTSOURCE_MMC || !instance) {
		if (bootsource != BOOTSOURCE_SPI) {
			pr_err("Unhandled boot source: %s, instance %i\n",
			       bootsource_to_string(bootsource), instance);
		} else
			of_device_enable_path("/chosen/environment-qspi");
	} else {
		of_device_enable_path("/chosen/environment-sd");
		mm_sm_sama5d2_fuse();
	}

	defaultenv_append_directory(defaultenv_mm_sm_sama5d2);

	return 0;
}
device_initcall(mm_sm_sama5d2_init);

static int __init mm_sm_sama5d2_clk_init(void)
{
	if (!of_machine_is_compatible("atmel,sama5d2"))
		return 0;

	/* Tune CLK subsystem */
	return clk_name_set_rate("pllack", 960000000);
}
coredevice_initcall(mm_sm_sama5d2_clk_init);
