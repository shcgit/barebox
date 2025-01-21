// SPDX-License-Identifier: GPL-2.0
/*
 * Rockchip PCIE3.0 phy driver
 *
 * Copyright (C) 2022 Rockchip Electronics Co., Ltd.
 */

#include <of.h>
#include <common.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/kernel.h>
//#include <linux/phy/pcie.h>
#include <mfd/syscon.h>
#include <linux/phy/phy.h>
#include <linux/regmap.h>
#include <linux/reset.h>

/* Register for RK3568 */
#define GRF_PCIE30PHY_CON1			0x4
#define GRF_PCIE30PHY_CON6			0x18
#define GRF_PCIE30PHY_CON9			0x24
#define GRF_PCIE30PHY_DA_OCM			(BIT(15) | BIT(31))
#define GRF_PCIE30PHY_STATUS0			0x80
#define GRF_PCIE30PHY_WR_EN			(0xf << 16)
#define SRAM_INIT_DONE(reg)			(reg & BIT(14))

#define RK3568_BIFURCATION_LANE_0_1		BIT(0)

/* Register for RK3588 */
#define PHP_GRF_PCIESEL_CON			0x100
#define RK3588_PCIE3PHY_GRF_CMN_CON0		0x0
#define RK3588_PCIE3PHY_GRF_PHY0_STATUS1	0x904
#define RK3588_PCIE3PHY_GRF_PHY1_STATUS1	0xa04
#define RK3588_SRAM_INIT_DONE(reg)		(reg & BIT(0))

#define RK3588_BIFURCATION_LANE_0_1		BIT(0)
#define RK3588_BIFURCATION_LANE_2_3		BIT(1)
#define RK3588_LANE_AGGREGATION		BIT(2)

struct rockchip_p3phy_ops;

struct rockchip_p3phy_priv {
	const struct rockchip_p3phy_ops *ops;
	void __iomem *mmio;
	/* mode: RC, EP */
	int mode;
	/* pcie30_phymode: Aggregation, Bifurcation */
	int pcie30_phymode;
	struct regmap *phy_grf;
	struct regmap *pipe_grf;
	struct reset_control *p30phy;
	struct phy *phy;
	struct clk_bulk_data *clks;
	int num_clks;
	int num_lanes;
	u32 lanes[4];
};

struct rockchip_p3phy_ops {
	int (*phy_init)(struct rockchip_p3phy_priv *priv);
};

static int rockchip_p3phy_set_mode(struct phy *phy, enum phy_mode mode, int submode)
{
	return 0;
}

static int rockchip_p3phy_rk3568_init(struct rockchip_p3phy_priv *priv)
{
	struct phy *phy = priv->phy;
	bool bifurcation = false;
	int ret;
	u32 reg;

	/* Deassert PCIe PMA output clamp mode */
	regmap_write(priv->phy_grf, GRF_PCIE30PHY_CON9, GRF_PCIE30PHY_DA_OCM);

	for (int i = 0; i < priv->num_lanes; i++) {
		dev_info(&phy->dev, "lane number %d, val %d\n", i, priv->lanes[i]);
		if (priv->lanes[i] > 1)
			bifurcation = true;
	}

	/* Set bifurcation if needed, and it doesn't care RC/EP */
	if (bifurcation) {
		dev_info(&phy->dev, "bifurcation enabled\n");
		regmap_write(priv->phy_grf, GRF_PCIE30PHY_CON6,
			     GRF_PCIE30PHY_WR_EN | RK3568_BIFURCATION_LANE_0_1);
		regmap_write(priv->phy_grf, GRF_PCIE30PHY_CON1,
			     GRF_PCIE30PHY_DA_OCM);
	} else {
		dev_dbg(&phy->dev, "bifurcation disabled\n");
		regmap_write(priv->phy_grf, GRF_PCIE30PHY_CON6,
			     GRF_PCIE30PHY_WR_EN & ~RK3568_BIFURCATION_LANE_0_1);
	}

	reset_control_deassert(priv->p30phy);

	ret = regmap_read_poll_timeout(priv->phy_grf,
				       GRF_PCIE30PHY_STATUS0,
				       reg, SRAM_INIT_DONE(reg),
				       500);
	if (ret)
		dev_err(&priv->phy->dev, "%s: lock failed 0x%x, check input refclk and power supply\n",
		       __func__, reg);
	return ret;
}

static const struct rockchip_p3phy_ops rk3568_ops = {
	.phy_init = rockchip_p3phy_rk3568_init,
};

static int rockchip_p3phy_rk3588_init(struct rockchip_p3phy_priv *priv)
{
	u32 reg = 0;
	u8 mode = 0;
	int ret;

	/* Deassert PCIe PMA output clamp mode */
	regmap_write(priv->phy_grf, RK3588_PCIE3PHY_GRF_CMN_CON0, BIT(8) | BIT(24));

	/* Set bifurcation if needed */
	for (int i = 0; i < priv->num_lanes; i++) {
		if (!priv->lanes[i])
			mode |= (BIT(i) << 3);

		if (priv->lanes[i] > 1)
			mode |= (BIT(i) >> 1);
	}

	if (!mode)
		reg = RK3588_LANE_AGGREGATION;
	else {
		if (mode & (BIT(0) | BIT(1)))
			reg |= RK3588_BIFURCATION_LANE_0_1;

		if (mode & (BIT(2) | BIT(3)))
			reg |= RK3588_BIFURCATION_LANE_2_3;
	}

	regmap_write(priv->phy_grf, RK3588_PCIE3PHY_GRF_CMN_CON0, (0x7<<16) | reg);

	/* Set pcie1ln_sel in PHP_GRF_PCIESEL_CON */
	if (!IS_ERR(priv->pipe_grf)) {
		reg = (mode & (BIT(6) | BIT(7))) >> 6;
		if (reg)
			regmap_write(priv->pipe_grf, PHP_GRF_PCIESEL_CON,
				     (reg << 16) | reg);
	}

	reset_control_deassert(priv->p30phy);

	ret = regmap_read_poll_timeout(priv->phy_grf,
				       RK3588_PCIE3PHY_GRF_PHY0_STATUS1,
				       reg, RK3588_SRAM_INIT_DONE(reg),
				       500);
	ret |= regmap_read_poll_timeout(priv->phy_grf,
					RK3588_PCIE3PHY_GRF_PHY1_STATUS1,
					reg, RK3588_SRAM_INIT_DONE(reg),
					500);
	if (ret)
		dev_err(&priv->phy->dev, "lock failed 0x%x, check input refclk and power supply\n",
			reg);
	return ret;
}

static const struct rockchip_p3phy_ops rk3588_ops = {
	.phy_init = rockchip_p3phy_rk3588_init,
};

static int rochchip_p3phy_init(struct phy *phy)
{
	struct rockchip_p3phy_priv *priv = phy_get_drvdata(phy);
	int ret;

	ret = clk_bulk_prepare_enable(priv->num_clks, priv->clks);
	if (ret) {
		dev_err(&priv->phy->dev, "failed to enable PCIe bulk clks %d\n", ret);
		return ret;
	}

	reset_control_assert(priv->p30phy);
	udelay(1);

	if (priv->ops->phy_init) {
		ret = priv->ops->phy_init(priv);
		if (ret)
			clk_bulk_disable_unprepare(priv->num_clks, priv->clks);
	}

	return ret;
}

static int rochchip_p3phy_exit(struct phy *phy)
{
	struct rockchip_p3phy_priv *priv = phy_get_drvdata(phy);

	clk_bulk_disable_unprepare(priv->num_clks, priv->clks);
	reset_control_assert(priv->p30phy);
	return 0;
}

static const struct phy_ops rochchip_p3phy_ops = {
	.init = rochchip_p3phy_init,
	.exit = rochchip_p3phy_exit,
	.set_mode = rockchip_p3phy_set_mode,
};

static struct phy *rockchip_p3phy_xlate(struct device *dev,
					const struct of_phandle_args *args)
{
	struct rockchip_p3phy_priv *priv = dev->priv;

	return priv->phy;
}

static int rockchip_p3phy_probe(struct device *dev)
{
	struct phy_provider *phy_provider;
	struct rockchip_p3phy_priv *priv;
	struct device_node *np = dev->of_node;
	struct resource *res;
	int ret;

	priv = xzalloc(sizeof(*priv));

	res = dev_request_mem_resource(dev, 0);
	if (IS_ERR(res)) {
		ret = PTR_ERR(res);
		return ret;
	}

	priv->mmio = IOMEM(res->start);
	if (IS_ERR(priv->mmio)) {
		ret = PTR_ERR(priv->mmio);
		return ret;
	}

	priv->ops = device_get_match_data(dev);
	if (!priv->ops) {
		dev_err(dev, "no of match data provided\n");
		return -EINVAL;
	}

	priv->phy_grf = syscon_regmap_lookup_by_phandle(np, "rockchip,phy-grf");
	if (IS_ERR(priv->phy_grf)) {
		dev_err(dev, "failed to find rockchip,phy_grf regmap\n");
		return PTR_ERR(priv->phy_grf);
	}

	if (of_device_is_compatible(np, "rockchip,rk3588-pcie3-phy")) {
		priv->pipe_grf =
			syscon_regmap_lookup_by_phandle(dev->of_node,
							"rockchip,pipe-grf");
		if (IS_ERR(priv->pipe_grf))
			dev_info(dev, "failed to find rockchip,pipe_grf regmap\n");
	} else {
		priv->pipe_grf = NULL;
	}

	priv->num_lanes = of_property_read_variable_u32_array(dev->of_node, "data-lanes",
							     priv->lanes, 2,
							     ARRAY_SIZE(priv->lanes));

	/* if no data-lanes assume aggregation */
	if (priv->num_lanes == -EINVAL) {
		dev_dbg(dev, "no data-lanes property found\n");
		priv->num_lanes = 1;
		priv->lanes[0] = 1;
	} else if (priv->num_lanes < 0) {
		dev_err(dev, "failed to read data-lanes property %d\n", priv->num_lanes);
		return priv->num_lanes;
	}

	priv->phy = phy_create(dev, NULL, &rochchip_p3phy_ops);
	if (IS_ERR(priv->phy)) {
		dev_err(dev, "failed to create combphy\n");
		return PTR_ERR(priv->phy);
	}

	priv->p30phy = reset_control_get_optional(dev, "phy");
	if (IS_ERR(priv->p30phy)) {
		return dev_err_probe(dev, PTR_ERR(priv->p30phy),
				     "failed to get phy reset control\n");
	}
	if (!priv->p30phy)
		dev_info(dev, "no phy reset control specified\n");

	priv->num_clks = clk_bulk_get_all(dev, &priv->clks);
	if (priv->num_clks < 1)
		return -ENODEV;

	dev->priv = priv;
	phy_set_drvdata(priv->phy, priv);
	phy_provider = of_phy_provider_register(dev, rockchip_p3phy_xlate);

	return PTR_ERR_OR_ZERO(phy_provider);
}

static const struct of_device_id rockchip_p3phy_of_match[] = {
	{ .compatible = "rockchip,rk3568-pcie3-phy", .data = &rk3568_ops },
	{ .compatible = "rockchip,rk3588-pcie3-phy", .data = &rk3588_ops },
	{ },
};
MODULE_DEVICE_TABLE(of, rockchip_p3phy_of_match);

static struct driver rockchip_p3phy_driver = {
	.probe	= rockchip_p3phy_probe,
	.name = "rockchip-snps-pcie3-phy",
	.of_compatible = rockchip_p3phy_of_match,
};
coredevice_platform_driver(rockchip_p3phy_driver);

MODULE_DESCRIPTION("Rockchip Synopsys PCIe 3.0 PHY driver");
MODULE_LICENSE("GPL");
