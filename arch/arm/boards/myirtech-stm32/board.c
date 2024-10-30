// SPDX-License-Identifier: GPL-2.0+

#include <bootsource.h>
#include <common.h>
#include <deep-probe.h>
#include <envfs.h>
#include <environment.h>
#include <init.h>
#include <net.h>
#include <of.h>
#include <linux/nvmem-consumer.h>
#include <mach/stm32mp/bbu.h>

static int stm32mp15x_myirtech_mmc1_fixup(struct device_node *root, void *unused)
{
	of_device_disable_by_alias("mmc0");

	return 0;
}

static char *stm32mp15x_myirtech_read_nvmem(const char *node, const char *name,
					    size_t size)
{
	struct device_node *np;

	np = of_find_node_by_name_address(NULL, node);
	if (!np) {
		pr_err("Can't read NVMEM %s\n", name);
		return ERR_PTR(-ENOENT);
	}

	return nvmem_cell_get_and_read(np, name, size);
}

#define FIELD_SZ	64

static void stm32mp15x_myirtech_nvmem_init(void)
{
	char str[FIELD_SZ], *data;
	struct device_node *np;
	struct eth_device *eth;
	u8 mac[ETH_ALEN];
	int len, i, j;

	data = stm32mp15x_myirtech_read_nvmem("product-info", "product-name",
					      FIELD_SZ);
	if (IS_ERR(data))
		return;

	len = data[0] - '0';
	if ((len < 8) || (len > (FIELD_SZ - 1))) {
		pr_err("Unable to get product-name\n");
		kfree(data);
		return;
	}

	strncpy(str, &data[1], len);
	str[len] = '\0';
	pr_info("Product name: %s\n", str);
	barebox_set_model(str);
	kfree(data);

	data = stm32mp15x_myirtech_read_nvmem("serial-info", "serial-number",
					      FIELD_SZ);
	if (IS_ERR(data))
		return;

	len = data[0] - '0';
	if ((len < 8) || (len > (FIELD_SZ - 1))) {
		pr_err("Unable to get serial-number\n");
		kfree(data);
		return;
	}

	strncpy(str, &data[1], len);
	str[len] = '\0';
	pr_info("Product serial: %s\n", str);
	barebox_set_serial_number(str);
	kfree(data);

	np = of_find_node_by_alias(of_get_root_node(), "ethernet0");
	if (!np) {
		pr_err("Unable to get ethernet0 alias\n");
		return;
	}

	eth = of_find_eth_device_by_node(np);
	if (!eth) {
		pr_err("Unable to get ethernet device\n");
		return;
	}

	if (is_valid_ether_addr(eth->ethaddr))
		return;

	/* Make fixed MAC-address based on serial number */
	memcpy(mac, str, sizeof(mac));
	for (i = sizeof(mac); i < len; i++) {
		for (j = 0; j < sizeof(mac); j++)
			mac[j] ^= str[i];
	}
	mac[0] &= 0xfe; /* clear multicast bit */
	mac[0] |= 0x02; /* set local assignment bit (IEEE802) */

	eth_register_ethaddr(0, mac);
}

static int stm32mp15x_myirtech_probe(struct device *dev)
{
	int instance = bootsource_get_instance(), def = -1;
	const enum bootsource bs = bootsource_get();

	barebox_set_hostname("myir-stm32");

	stm32mp15x_myirtech_nvmem_init();

	switch (bs) {
	case BOOTSOURCE_NAND:
		of_device_enable_path("/chosen/environment-nand");
		pr_info("Boot from NAND...\n");
		def = 2;
		break;
	case BOOTSOURCE_MMC:
		def = instance;
		if (instance == 0) {
			of_device_enable_path("/chosen/environment-sd");
			pr_info("Boot from SD...\n");
			break;
		} else if (instance == 1) {
			of_device_enable_path("/chosen/environment-emmc");
			pr_info("Boot from eMMC...\n");
			break;
		}
		fallthrough;
	default:
		pr_err("Unhandled bootsource %i, instance %i\n", bs, instance);
		return 0;
	}

	stm32mp_bbu_mmc_fip_register("sd", "/dev/mmc0",
				     (def == 0) ? BBU_HANDLER_FLAG_DEFAULT : 0);

	stm32mp_bbu_mmc_fip_register("emmc", "/dev/mmc1",
				     (def == 1) ? BBU_HANDLER_FLAG_DEFAULT : 0);

	//TODO: NAND

	if (IS_ENABLED(CONFIG_DEFAULT_ENVIRONMENT))
		defaultenv_append_directory(defaultenv_myirtech_stm32);

	of_register_fixup(stm32mp15x_myirtech_mmc1_fixup, NULL);

	return 0;
}

static const struct of_device_id stm32mp15x_myirtech_of_match[] = {
	{ .compatible = "myir,myd-ya151c-t" },
	{ },
};
BAREBOX_DEEP_PROBE_ENABLE(stm32mp15x_myirtech_of_match);

static struct driver stm32mp15x_myirtech_board_driver = {
	.name = "board-myirtech-stm32",
	.probe = stm32mp15x_myirtech_probe,
	.of_compatible = stm32mp15x_myirtech_of_match,
};
device_platform_driver(stm32mp15x_myirtech_board_driver);
