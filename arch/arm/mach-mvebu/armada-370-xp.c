/*
 * Copyright
 * (C) 2013 Thomas Petazzoni <thomas.petazzoni@free-electrons.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <common.h>
#include <init.h>
#include <io.h>
#include <of.h>
#include <of_address.h>
#include <asm/memory.h>
#include <linux/mbus.h>
#include <mach/armada-370-xp-regs.h>
#include <mach/socid.h>

static inline void armada_370_xp_memory_find(unsigned long *phys_base,
					     unsigned long *phys_size)
{
	int cs;

	*phys_base = ~0;
	*phys_size = 0;

	for (cs = 0; cs < 4; cs++) {
		u32 base = readl(ARMADA_370_XP_SDRAM_BASE + DDR_BASE_CSn(cs));
		u32 ctrl = readl(ARMADA_370_XP_SDRAM_BASE + DDR_SIZE_CSn(cs));

		/* Skip non-enabled CS */
		if ((ctrl & DDR_SIZE_ENABLED) != DDR_SIZE_ENABLED)
			continue;

		base &= DDR_BASE_CS_LOW_MASK;
		if (base < *phys_base)
			*phys_base = base;
		*phys_size += (ctrl | ~DDR_SIZE_MASK) + 1;
	}
}

static const struct of_device_id armada_370_xp_pcie_of_ids[] = {
	{ .compatible = "marvell,armada-xp-pcie", },
	{ .compatible = "marvell,armada-370-pcie", },
	{ },
};

static int armada_370_xp_soc_id_fixup(void)
{
	struct device_node *np, *cnp;
	void __iomem *base;
	u32 reg, ctrl, mask;
	u32 socid, numcpus;

	socid = readl(ARMADA_370_XP_CPU_SOC_ID) & CPU_SOC_ID_DEVICE_MASK;
	numcpus = 1 + (readl(ARMADA_370_XP_FABRIC_CONF) & FABRIC_NUM_CPUS_MASK);

	switch (socid) {
	/*
	 * Marvell Armada XP MV78230-A0 incorrectly identifies itself as
	 * MV78460. Check for DEVID_MV78460 but if there are only 2 CPUs
	 * present in Coherency Fabric, fixup PCIe PRODUCT_ID.
	 */
	case DEVID_MV78460:
		if (numcpus != 2)
			return 0;
		socid = DEVID_MV78230;
		mask = PCIE0_EN | PCIE1_EN | PCIE0_QUADX1_EN;
		break;
	default:
		return 0;
	}

	np = of_find_matching_node(NULL, armada_370_xp_pcie_of_ids);
	if (!np)
		return -ENODEV;

	/* Enable all individual x1 ports */
	ctrl = readl(ARMADA_370_XP_SOC_CTRL);
	writel(ctrl | mask, ARMADA_370_XP_SOC_CTRL);

	for_each_child_of_node(np, cnp) {
		base = of_iomap(cnp, 0);
		if (!base)
			continue;

		/* Fixup PCIe port DEVICE_ID */
		reg = readl(base + PCIE_VEN_DEV_ID);
		reg = (socid << 16) | (reg & 0xffff);
		writel(reg, base + PCIE_VEN_DEV_ID);
	}

	/* Restore SoC Control */
	writel(ctrl, ARMADA_370_XP_SOC_CTRL);

	return 0;
}

static void __noreturn armada_370_xp_reset_cpu(unsigned long addr)
{
	writel(0x1, ARMADA_370_XP_SYSCTL_BASE + 0x60);
	writel(0x1, ARMADA_370_XP_SYSCTL_BASE + 0x64);
	while (1)
		;
}

static int armada_xp_init_soc(struct device_node *root)
{
	u32 reg;

	/* Enable GBE0, GBE1, LCD and NFC PUP */
	reg = readl(ARMADA_XP_PUP_ENABLE);
	reg |= GE0_PUP_EN | GE1_PUP_EN | LCD_PUP_EN | NAND_PUP_EN | SPI_PUP_EN;
	writel(reg, ARMADA_XP_PUP_ENABLE);

	return 0;
}

static int armada_370_xp_init_soc(struct device_node *root, void *context)
{
	unsigned long phys_base, phys_size;
	u32 reg;

	if (!of_machine_is_compatible("marvell,armada-370-xp"))
		return 0;

	mvebu_set_reset(armada_370_xp_reset_cpu);

	barebox_set_model("Marvell Armada 370/XP");
	barebox_set_hostname("armada");

	/* Disable MBUS error propagation */
	reg = readl(ARMADA_370_XP_FABRIC_CTRL);
	reg &= ~MBUS_ERR_PROP_EN;
	writel(reg, ARMADA_370_XP_FABRIC_CTRL);

	armada_370_xp_memory_find(&phys_base, &phys_size);

	mvebu_set_memory(phys_base, phys_size);
	mvebu_mbus_init();

	armada_370_xp_soc_id_fixup();

	if (of_machine_is_compatible("marvell,armadaxp"))
		armada_xp_init_soc(root);

	return 0;
}

static int armada_370_xp_register_soc_fixup(void)
{
	mvebu_mbus_add_range("marvell,armada-370-xp", 0xf0, 0x01,
			     MVEBU_REMAP_INT_REG_BASE);
	return of_register_fixup(armada_370_xp_init_soc, NULL);
}
pure_initcall(armada_370_xp_register_soc_fixup);
