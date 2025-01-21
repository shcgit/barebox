// SPDX-License-Identifier: GPL-2.0-only
/*
 * MFD core driver for Rockchip RK808/RK818
 *
 * Copyright (c) 2014, Fuzhou Rockchip Electronics Co., Ltd
 *
 * Author: Chris Zhong <zyw@rock-chips.com>
 * Author: Zhang Qing <zhangqing@rock-chips.com>
 *
 * Copyright (C) 2016 PHYTEC Messtechnik GmbH
 *
 * Author: Wadim Egorov <w.egorov@phytec.de>
 */

#define pr_fmt(fmt) "rk808: " fmt

#include <common.h>
#include <i2c/i2c.h>
#include <linux/mfd/rk808.h>
#include <linux/mfd/core.h>
#include <driver.h>
#include <poweroff.h>
#include <of.h>
#include <linux/regmap.h>
#include <spi/spi.h>

struct rk808_reg_data {
	int addr;
	int mask;
	int value;
};

static const struct regmap_config rk818_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = RK818_USB_CTRL_REG,
};

static const struct regmap_config rk805_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = RK805_OFF_SOURCE_REG,
};

static const struct regmap_config rk808_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = RK808_IO_POL_REG,
};

static const struct regmap_config rk817_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = RK817_GPIO_INT_CFG,
};

static const struct mfd_cell rk805s[] = {
	{ .name = "rk808-clkout", },
	{ .name = "rk808-regulator", },
	{ .name = "rk805-pinctrl", },
	{ .name = "rk808-rtc", },
	{ .name = "rk805-pwrkey", },
};

static const struct mfd_cell rk806s[] = {
	{ .name = "rk805-pinctrl", },
	{ .name = "rk808-regulator", },
	{ .name = "rk805-pwrkey", },
};

static const struct mfd_cell rk808s[] = {
	{ .name = "rk808-clkout", },
	{ .name = "rk808-regulator", },
	{ .name = "rk808-rtc", },
};

static const struct mfd_cell rk817s[] = {
	{ .name = "rk808-clkout", },
	{ .name = "rk808-regulator", },
	{ .name = "rk805-pwrkey", },
	{ .name = "rk808-rtc", },
	{ .name = "rk817-codec", },
};

static const struct mfd_cell rk818s[] = {
	{ .name = "rk808-clkout", },
	{ .name = "rk808-regulator", },
	{ .name = "rk808-rtc", },
};

static const struct rk808_reg_data rk805_pre_init_reg[] = {
	{RK805_BUCK1_CONFIG_REG, RK805_BUCK1_2_ILMAX_MASK,
				 RK805_BUCK1_2_ILMAX_4000MA},
	{RK805_BUCK2_CONFIG_REG, RK805_BUCK1_2_ILMAX_MASK,
				 RK805_BUCK1_2_ILMAX_4000MA},
	{RK805_BUCK3_CONFIG_REG, RK805_BUCK3_4_ILMAX_MASK,
				 RK805_BUCK3_ILMAX_3000MA},
	{RK805_BUCK4_CONFIG_REG, RK805_BUCK3_4_ILMAX_MASK,
				 RK805_BUCK4_ILMAX_3500MA},
	{RK805_BUCK4_CONFIG_REG, BUCK_ILMIN_MASK, BUCK_ILMIN_400MA},
	{RK805_THERMAL_REG, TEMP_HOTDIE_MSK, TEMP115C},
};

static const struct rk808_reg_data rk806_pre_init_reg[] = {
	{ RK806_GPIO_INT_CONFIG, RK806_INT_POL_MSK, RK806_INT_POL_L },
	{ RK806_SYS_CFG3, RK806_SLAVE_RESTART_FUN_MSK, RK806_SLAVE_RESTART_FUN_EN },
	{ RK806_SYS_OPTION, RK806_SYS_ENB2_2M_MSK, RK806_SYS_ENB2_2M_EN },
};

static const struct rk808_reg_data rk808_pre_init_reg[] = {
	{ RK808_BUCK3_CONFIG_REG, BUCK_ILMIN_MASK,  BUCK_ILMIN_150MA },
	{ RK808_BUCK4_CONFIG_REG, BUCK_ILMIN_MASK,  BUCK_ILMIN_200MA },
	{ RK808_BOOST_CONFIG_REG, BOOST_ILMIN_MASK, BOOST_ILMIN_100MA },
	{ RK808_BUCK1_CONFIG_REG, BUCK1_RATE_MASK,  BUCK_ILMIN_200MA },
	{ RK808_BUCK2_CONFIG_REG, BUCK2_RATE_MASK,  BUCK_ILMIN_200MA },
	{ RK808_DCDC_UV_ACT_REG,  BUCK_UV_ACT_MASK, BUCK_UV_ACT_DISABLE},
	{ RK808_VB_MON_REG,       MASK_ALL,         VB_LO_ACT |
						    VB_LO_SEL_3500MV },
};

static const struct rk808_reg_data rk817_pre_init_reg[] = {
	{RK817_RTC_CTRL_REG, RTC_STOP, RTC_STOP},
	/* Codec specific registers */
	{ RK817_CODEC_DTOP_VUCTL, MASK_ALL, 0x03 },
	{ RK817_CODEC_DTOP_VUCTIME, MASK_ALL, 0x00 },
	{ RK817_CODEC_DTOP_LPT_SRST, MASK_ALL, 0x00 },
	{ RK817_CODEC_DTOP_DIGEN_CLKE, MASK_ALL, 0x00 },
	/* from vendor driver, CODEC_AREF_RTCFG0 not defined in data sheet */
	{ RK817_CODEC_AREF_RTCFG0, MASK_ALL, 0x00 },
	{ RK817_CODEC_AREF_RTCFG1, MASK_ALL, 0x06 },
	{ RK817_CODEC_AADC_CFG0, MASK_ALL, 0xc8 },
	/* from vendor driver, CODEC_AADC_CFG1 not defined in data sheet */
	{ RK817_CODEC_AADC_CFG1, MASK_ALL, 0x00 },
	{ RK817_CODEC_DADC_VOLL, MASK_ALL, 0x00 },
	{ RK817_CODEC_DADC_VOLR, MASK_ALL, 0x00 },
	{ RK817_CODEC_DADC_SR_ACL0, MASK_ALL, 0x00 },
	{ RK817_CODEC_DADC_ALC1, MASK_ALL, 0x00 },
	{ RK817_CODEC_DADC_ALC2, MASK_ALL, 0x00 },
	{ RK817_CODEC_DADC_NG, MASK_ALL, 0x00 },
	{ RK817_CODEC_DADC_HPF, MASK_ALL, 0x00 },
	{ RK817_CODEC_DADC_RVOLL, MASK_ALL, 0xff },
	{ RK817_CODEC_DADC_RVOLR, MASK_ALL, 0xff },
	{ RK817_CODEC_AMIC_CFG0, MASK_ALL, 0x70 },
	{ RK817_CODEC_AMIC_CFG1, MASK_ALL, 0x00 },
	{ RK817_CODEC_DMIC_PGA_GAIN, MASK_ALL, 0x66 },
	{ RK817_CODEC_DMIC_LMT1, MASK_ALL, 0x00 },
	{ RK817_CODEC_DMIC_LMT2, MASK_ALL, 0x00 },
	{ RK817_CODEC_DMIC_NG1, MASK_ALL, 0x00 },
	{ RK817_CODEC_DMIC_NG2, MASK_ALL, 0x00 },
	/* from vendor driver, CODEC_ADAC_CFG0 not defined in data sheet */
	{ RK817_CODEC_ADAC_CFG0, MASK_ALL, 0x00 },
	{ RK817_CODEC_ADAC_CFG1, MASK_ALL, 0x07 },
	{ RK817_CODEC_DDAC_POPD_DACST, MASK_ALL, 0x82 },
	{ RK817_CODEC_DDAC_VOLL, MASK_ALL, 0x00 },
	{ RK817_CODEC_DDAC_VOLR, MASK_ALL, 0x00 },
	{ RK817_CODEC_DDAC_SR_LMT0, MASK_ALL, 0x00 },
	{ RK817_CODEC_DDAC_LMT1, MASK_ALL, 0x00 },
	{ RK817_CODEC_DDAC_LMT2, MASK_ALL, 0x00 },
	{ RK817_CODEC_DDAC_MUTE_MIXCTL, MASK_ALL, 0xa0 },
	{ RK817_CODEC_DDAC_RVOLL, MASK_ALL, 0xff },
	{ RK817_CODEC_DADC_RVOLR, MASK_ALL, 0xff },
	{ RK817_CODEC_AMIC_CFG0, MASK_ALL, 0x70 },
	{ RK817_CODEC_AMIC_CFG1, MASK_ALL, 0x00 },
	{ RK817_CODEC_DMIC_PGA_GAIN, MASK_ALL, 0x66 },
	{ RK817_CODEC_DMIC_LMT1, MASK_ALL, 0x00 },
	{ RK817_CODEC_DMIC_LMT2, MASK_ALL, 0x00 },
	{ RK817_CODEC_DMIC_NG1, MASK_ALL, 0x00 },
	{ RK817_CODEC_DMIC_NG2, MASK_ALL, 0x00 },
	/* from vendor driver, CODEC_ADAC_CFG0 not defined in data sheet */
	{ RK817_CODEC_ADAC_CFG0, MASK_ALL, 0x00 },
	{ RK817_CODEC_ADAC_CFG1, MASK_ALL, 0x07 },
	{ RK817_CODEC_DDAC_POPD_DACST, MASK_ALL, 0x82 },
	{ RK817_CODEC_DDAC_VOLL, MASK_ALL, 0x00 },
	{ RK817_CODEC_DDAC_VOLR, MASK_ALL, 0x00 },
	{ RK817_CODEC_DDAC_SR_LMT0, MASK_ALL, 0x00 },
	{ RK817_CODEC_DDAC_LMT1, MASK_ALL, 0x00 },
	{ RK817_CODEC_DDAC_LMT2, MASK_ALL, 0x00 },
	{ RK817_CODEC_DDAC_MUTE_MIXCTL, MASK_ALL, 0xa0 },
	{ RK817_CODEC_DDAC_RVOLL, MASK_ALL, 0xff },
	{ RK817_CODEC_DDAC_RVOLR, MASK_ALL, 0xff },
	{ RK817_CODEC_AHP_ANTI0, MASK_ALL, 0x00 },
	{ RK817_CODEC_AHP_ANTI1, MASK_ALL, 0x00 },
	{ RK817_CODEC_AHP_CFG0, MASK_ALL, 0xe0 },
	{ RK817_CODEC_AHP_CFG1, MASK_ALL, 0x1f },
	{ RK817_CODEC_AHP_CP, MASK_ALL, 0x09 },
	{ RK817_CODEC_ACLASSD_CFG1, MASK_ALL, 0x69 },
	{ RK817_CODEC_ACLASSD_CFG2, MASK_ALL, 0x44 },
	{ RK817_CODEC_APLL_CFG0, MASK_ALL, 0x04 },
	{ RK817_CODEC_APLL_CFG1, MASK_ALL, 0x00 },
	{ RK817_CODEC_APLL_CFG2, MASK_ALL, 0x30 },
	{ RK817_CODEC_APLL_CFG3, MASK_ALL, 0x19 },
	{ RK817_CODEC_APLL_CFG4, MASK_ALL, 0x65 },
	{ RK817_CODEC_APLL_CFG5, MASK_ALL, 0x01 },
	{ RK817_CODEC_DI2S_CKM, MASK_ALL, 0x01 },
	{ RK817_CODEC_DI2S_RSD, MASK_ALL, 0x00 },
	{ RK817_CODEC_DI2S_RXCR1, MASK_ALL, 0x00 },
	{ RK817_CODEC_DI2S_RXCR2, MASK_ALL, 0x17 },
	{ RK817_CODEC_DI2S_RXCMD_TSD, MASK_ALL, 0x00 },
	{ RK817_CODEC_DI2S_TXCR1, MASK_ALL, 0x00 },
	{ RK817_CODEC_DI2S_TXCR2, MASK_ALL, 0x17 },
	{ RK817_CODEC_DI2S_TXCR3_TXCMD, MASK_ALL, 0x00 },
	{RK817_GPIO_INT_CFG, RK817_INT_POL_MSK, RK817_INT_POL_L},
	{RK817_SYS_CFG(1), RK817_HOTDIE_TEMP_MSK | RK817_TSD_TEMP_MSK,
					   RK817_HOTDIE_105 | RK817_TSD_140},
};

static const struct rk808_reg_data rk818_pre_init_reg[] = {
	/* improve efficiency */
	{ RK818_BUCK2_CONFIG_REG, BUCK2_RATE_MASK,  BUCK_ILMIN_250MA },
	{ RK818_BUCK4_CONFIG_REG, BUCK_ILMIN_MASK,  BUCK_ILMIN_250MA },
	{ RK818_BOOST_CONFIG_REG, BOOST_ILMIN_MASK, BOOST_ILMIN_100MA },
	{ RK818_USB_CTRL_REG,	  RK818_USB_ILIM_SEL_MASK,
						    RK818_USB_ILMIN_2000MA },
	/* close charger when usb lower then 3.4V */
	{ RK818_USB_CTRL_REG,	  RK818_USB_CHG_SD_VSEL_MASK,
						    (0x7 << 4) },
	/* no action when vref */
	{ RK818_H5V_EN_REG,	  BIT(1),	    RK818_REF_RDY_CTRL },
	/* enable HDMI 5V */
	{ RK818_H5V_EN_REG,	  BIT(0),	    RK818_H5V_EN },
	{ RK808_VB_MON_REG,	  MASK_ALL,	    VB_LO_ACT |
						    VB_LO_SEL_3500MV },
};

static void rk808_poweroff(struct poweroff_handler *handler)
{
	struct rk808 *rk808 = container_of(handler, struct rk808, poweroff);
	int ret;
	unsigned int reg, bit;

	switch (rk808->variant) {
	case RK805_ID:
		reg = RK805_DEV_CTRL_REG;
		bit = DEV_OFF;
		break;
	case RK808_ID:
		reg = RK808_DEVCTRL_REG,
		bit = DEV_OFF_RST;
		break;
	case RK809_ID:
	case RK817_ID:
		reg = RK817_SYS_CFG(3);
		bit = DEV_OFF;
		break;
	case RK818_ID:
		reg = RK818_DEVCTRL_REG;
		bit = DEV_OFF;
		break;
	default:
		return;
	}

	shutdown_barebox();

	ret = regmap_update_bits(rk808->regmap, reg, bit, bit);
	if (ret)
		pr_err("Failed to shutdown device!\n");

	mdelay(1000);
	hang();
}

static int rk8xx_probe(struct device *dev, int variant, struct regmap *regmap)
{
	struct device_node *np = dev->of_node;
	struct rk808 *rk808;
	const struct rk808_reg_data *pre_init_reg;
	const struct mfd_cell *cells;
	int nr_pre_init_regs;
	int nr_cells;
	int ret;
	int i;

	rk808 = kzalloc(sizeof(*rk808), GFP_KERNEL);
	if (!rk808)
		return -ENOMEM;

	dev->priv = rk808;
	rk808->regmap = regmap;
	rk808->dev = dev;
	rk808->variant = variant;

	switch (rk808->variant) {
	case RK805_ID:
		pre_init_reg = rk805_pre_init_reg;
		nr_pre_init_regs = ARRAY_SIZE(rk805_pre_init_reg);
		cells = rk805s;
		nr_cells = ARRAY_SIZE(rk805s);
		break;
	case RK806_ID:
		pre_init_reg = rk806_pre_init_reg;
		nr_pre_init_regs = ARRAY_SIZE(rk806_pre_init_reg);
		cells = rk806s;
		nr_cells = ARRAY_SIZE(rk806s);
		break;
	case RK808_ID:
		pre_init_reg = rk808_pre_init_reg;
		nr_pre_init_regs = ARRAY_SIZE(rk808_pre_init_reg);
		cells = rk808s;
		nr_cells = ARRAY_SIZE(rk808s);
		break;
	case RK818_ID:
		pre_init_reg = rk818_pre_init_reg;
		nr_pre_init_regs = ARRAY_SIZE(rk818_pre_init_reg);
		cells = rk818s;
		nr_cells = ARRAY_SIZE(rk818s);
		break;
	case RK809_ID:
	case RK817_ID:
		pre_init_reg = rk817_pre_init_reg;
		nr_pre_init_regs = ARRAY_SIZE(rk817_pre_init_reg);
		cells = rk817s;
		nr_cells = ARRAY_SIZE(rk817s);
		break;
	default:
		dev_err(dev, "Unsupported RK8XX ID %lu\n",
			rk808->variant);
		return -EINVAL;
	}

	ret = regmap_register_cdev(rk808->regmap, NULL);
	if (ret)
		return ret;

	for (i = 0; i < nr_pre_init_regs; i++) {
		ret = regmap_update_bits(rk808->regmap,
					pre_init_reg[i].addr,
					pre_init_reg[i].mask,
					pre_init_reg[i].value);
		if (ret) {
			dev_err(dev,
				"0x%x write err\n",
				pre_init_reg[i].addr);
			return ret;
		}
	}

	ret = mfd_add_devices(dev, cells, nr_cells);
	if (ret) {
		dev_err(dev, "failed to add MFD devices %d\n", ret);
		return ret;
	}

	rk808->poweroff.name = "rk808-poweroff";
	rk808->poweroff.poweroff = rk808_poweroff;
	rk808->poweroff.priority = 200;

	if (of_property_read_bool(np, "rockchip,system-power-controller"))
		rk808->poweroff.priority += 100;

	poweroff_handler_register(&rk808->poweroff);

	return 0;
}

static int rk808_probe(struct device *dev)
{
	struct device_node *np = dev->of_node;
	struct i2c_client *client = to_i2c_client(dev);
	int msb, lsb;
	unsigned char pmic_id_msb, pmic_id_lsb;
	struct regmap *regmap;
	int variant;
	const struct regmap_config *regmap_cfg;

	if (of_device_is_compatible(np, "rockchip,rk817") ||
	    of_device_is_compatible(np, "rockchip,rk809")) {
		pmic_id_msb = RK817_ID_MSB;
		pmic_id_lsb = RK817_ID_LSB;
	} else {
		pmic_id_msb = RK808_ID_MSB;
		pmic_id_lsb = RK808_ID_LSB;
	}

	/* Read chip variant */
	msb = i2c_smbus_read_byte_data(client, pmic_id_msb);
	if (msb < 0) {
		dev_err(dev, "failed to read the chip id at 0x%x\n",
			RK808_ID_MSB);
		return msb;
	}

	lsb = i2c_smbus_read_byte_data(client, pmic_id_lsb);
	if (lsb < 0) {
		dev_err(dev, "failed to read the chip id at 0x%x\n",
			RK808_ID_LSB);
		return lsb;
	}

	variant = ((msb << 8) | lsb) & RK8XX_ID_MSK;
	dev_info(dev, "chip id: 0x%x\n", (unsigned int)variant);

	switch (variant) {
	case RK805_ID:
		regmap_cfg = &rk805_regmap_config;
		break;
	case RK808_ID:
		regmap_cfg = &rk808_regmap_config;
		break;
	case RK818_ID:
		regmap_cfg = &rk818_regmap_config;
		break;
	case RK809_ID:
	case RK817_ID:
		regmap_cfg = &rk817_regmap_config;
		break;
	default:
		dev_err(dev, "Unsupported RK8XX ID %u\n", variant);
		return -EINVAL;
	}

	regmap = regmap_init_i2c_smbus(client, regmap_cfg);
	if (IS_ERR(regmap)) {
		dev_err(dev, "regmap initialization failed\n");
		return PTR_ERR(regmap);
	}

	return rk8xx_probe(dev, variant, regmap);
}

#define RK806_ADDR_SIZE 2
#define RK806_CMD_WITH_SIZE(CMD, VALUE_BYTES) \
        (RK806_CMD_##CMD | RK806_CMD_CRC_DIS | (VALUE_BYTES - 1))

static const struct regmap_config rk806_regmap_config_spi = {
	.reg_bits = 16,
	.val_bits = 8,
	.max_register = RK806_BUCK_RSERVE_REG5,
};

static int rk806_spi_bus_write(void *context, const void *vdata, size_t count)
{
	struct device *dev = context;
	struct spi_device *spi = to_spi_device(dev);
	struct spi_transfer xfer[2] = { 0 };
	/* data and thus count includes the register address */
	size_t val_size = count - RK806_ADDR_SIZE;
	char cmd;

	if (val_size < 1 || val_size > (RK806_CMD_LEN_MSK + 1))
		return -EINVAL;

	cmd = RK806_CMD_WITH_SIZE(WRITE, val_size);

	xfer[0].tx_buf = &cmd;
	xfer[0].len = sizeof(cmd);
	xfer[1].tx_buf = vdata;
	xfer[1].len = count;

	return spi_sync_transfer(spi, xfer, ARRAY_SIZE(xfer));
}

static int rk806_spi_bus_read(void *context, const void *vreg, size_t reg_size,
			      void *val, size_t val_size)
{
	struct device *dev = context;
	struct spi_device *spi = to_spi_device(dev);
	char txbuf[3] = { 0 };

	if (reg_size != RK806_ADDR_SIZE ||
	    val_size < 1 || val_size > (RK806_CMD_LEN_MSK + 1))
		return -EINVAL;

	/* TX buffer contains command byte followed by two address bytes */
	txbuf[0] = RK806_CMD_WITH_SIZE(READ, val_size);
	memcpy(txbuf+1, vreg, reg_size);

	return spi_write_then_read(spi, txbuf, sizeof(txbuf), val, val_size);
}

static const struct regmap_bus rk806_regmap_bus_spi = {
	.write = rk806_spi_bus_write,
	.read = rk806_spi_bus_read,
	.reg_format_endian_default = REGMAP_ENDIAN_LITTLE,
};

static int rk808_probe_spi(struct device *dev)
{
	struct regmap *regmap;

	regmap = regmap_init(dev, &rk806_regmap_bus_spi,
				  dev, &rk806_regmap_config_spi);
	if (IS_ERR(regmap))
		return dev_err_probe(dev, PTR_ERR(regmap),
				     "Failed to init regmap\n");

	return rk8xx_probe(dev, RK806_ID, regmap);
}

static const struct of_device_id rk808_of_match[] = {
	{ .compatible = "rockchip,rk805" },
	{ .compatible = "rockchip,rk808" },
	{ .compatible = "rockchip,rk809" },
	{ .compatible = "rockchip,rk817" },
	{ .compatible = "rockchip,rk818" },
	{ },
};
MODULE_DEVICE_TABLE(of, rk808_of_match);

static __maybe_unused struct driver rk808_i2c_driver = {
	.name = "rk808",
	.of_compatible = rk808_of_match,
	.probe    = rk808_probe,
};
coredevice_i2c_driver(rk808_i2c_driver);

static const struct of_device_id rk808_spi_of_match[] = {
	{ .compatible = "rockchip,rk806" },
	{ },
};
MODULE_DEVICE_TABLE(of, rk808_of_match);

static __maybe_unused struct driver rk808_spi_driver = {
	.name = "rk808",
	.of_compatible = rk808_spi_of_match,
	.probe    = rk808_probe_spi,
};
coredevice_spi_driver(rk808_spi_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Chris Zhong <zyw@rock-chips.com>");
MODULE_AUTHOR("Zhang Qing <zhangqing@rock-chips.com>");
MODULE_AUTHOR("Wadim Egorov <w.egorov@phytec.de>");
MODULE_DESCRIPTION("RK808/RK818 PMIC driver");
