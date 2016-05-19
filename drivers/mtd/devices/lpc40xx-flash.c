/*
 * Copyright (C) 2014 Alexander Shiyan <shc_work@mail.ru>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 */

#include <common.h>
#include <environment.h>
#include <errno.h>
#include <init.h>
#include <io.h>
#include <malloc.h>
#include <sizes.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/mtd/mtd.h>

#define IAP_OFFSET	(0x1ff1)

/* Status codes */
#define IAP_CMD_SUCCESS					0
#define IAP_INVALID_COMMAND				1
#define IAP_SRC_ADDR_ERROR				2
#define IAP_DST_ADDR_ERROR				3
#define IAP_SRC_ADDR_NOT_MAPPED				4
#define IAP_DST_ADDR_NOT_MAPPED				5
#define IAP_COUNT_ERROR					6
#define IAP_INVALID_SECTOR				7
#define IAP_SECTOR_NOT_BLANK				8
#define IAP_SECTOR_NOT_PREPARED_FOR_WRITE_OPERATION	9
#define IAP_COMPARE_ERROR				10
#define IAP_BUSY					11
#define IAP_PARAM_ERROR					12
#define IAP_ADDR_ERROR					13
#define IAP_ADDR_NOT_MAPPED				14
#define IAP_CMD_LOCKED					15
#define IAP_INVALID_CODE				16
#define IAP_INVALID_BAUD_RATE				17
#define IAP_INVALID_STOP_BIT				18
#define IAP_CODE_READ_PROTECTION_ENABLED		19

/* Commands */
#define IAP_PREPARE					50
#define IAP_COPY_RAM2FLASH				51
#define IAP_ERASE					52
#define IAP_BLANK_CHECK					53
#define IAP_READ_PART_ID				54
#define IAP_READ_BOOT_VER				55
#define IAP_COMPARE					56
#define IAP_REINVOKE_ISP				57
#define IAP_READ_SERIAL_NUMBER				58

struct iap_cmd {
	u32	cmd;
	u32	param[4];
	u32	status;
	u32	result[4];
};

struct lpc40xx_flash {
	struct mtd_info	mtd;
	void __iomem	*base;
	void __iomem	*buff;
	unsigned long	clkrate;
	void		(*iap)(u32 *, u32 *);
};

static int lpc40xx_flash_iap(struct lpc40xx_flash *flash,
			     struct iap_cmd *command)
{
	flash->iap(&command->cmd, &command->status);

	switch (command->status) {
	case IAP_CMD_SUCCESS:
		return 0;
	case IAP_INVALID_COMMAND:
	case IAP_SRC_ADDR_ERROR:
	case IAP_DST_ADDR_ERROR:
	case IAP_SRC_ADDR_NOT_MAPPED:
	case IAP_DST_ADDR_NOT_MAPPED:
	case IAP_COUNT_ERROR:
	case IAP_INVALID_SECTOR:
	case IAP_PARAM_ERROR:
	case IAP_ADDR_ERROR:
	case IAP_ADDR_NOT_MAPPED:
		return -EINVAL;
	case IAP_BUSY:
		return -EBUSY;
	}

	return -EIO;
}

static u32 lpc40xx_flash_addr2sector(u32 addr)
{
	if (addr < 0x00010000)
		return (addr >> 12) & 0x0f;

	return 0x0e + ((addr >> 15) & 0x0f);
}

static int lpc40xx_flash_erase(struct mtd_info *mtd, struct erase_info *instr)
{
	struct lpc40xx_flash *flash = mtd->priv;
	struct iap_cmd cmd;
	int ret;

	instr->state = MTD_ERASE_FAILED;

	cmd.cmd = IAP_PREPARE;
	cmd.param[0] = lpc40xx_flash_addr2sector(instr->addr);
	cmd.param[1] = lpc40xx_flash_addr2sector(instr->addr + instr->len - 1);
	ret = lpc40xx_flash_iap(flash, &cmd);
	if (ret)
		return ret;

	cmd.cmd = IAP_ERASE;
	cmd.param[2] = flash->clkrate / 1000;
	ret = lpc40xx_flash_iap(flash, &cmd);
	if (ret)
		return ret;

	instr->state = MTD_ERASE_DONE;
	mtd_erase_callback(instr);

	return 0;
}

static int lpc40xx_flash_write(struct mtd_info *mtd, loff_t to, size_t len,
			       size_t *retlen, const u_char *buf)
{
	struct lpc40xx_flash *flash = mtd->priv;
	void *sptr = (void *)buf;
	struct iap_cmd cmd;
	u32 dptr = to;
	size_t count;
	int ret;

	*retlen = 0;

	if (to & 256)
		return -EINVAL;

	count = len;

	while (count) {
		u32 to_read, to_write;

		if (count >= 4096)
			to_write = 4096;
		else if (count >= 1024)
			to_write = 1024;
		else if (count >= 512)
			to_write = 512;
		else
			to_write = 256;

		to_read = count < to_write ? count : to_write;

		memcpy(flash->buff, sptr, to_read);

		cmd.cmd = IAP_PREPARE;
		cmd.param[0] = lpc40xx_flash_addr2sector(dptr);
		cmd.param[1] = lpc40xx_flash_addr2sector(dptr + to_write - 1);
		ret = lpc40xx_flash_iap(flash, &cmd);
		if (ret)
			return ret;

		cmd.cmd = IAP_COPY_RAM2FLASH;
		cmd.param[0] = dptr;
		cmd.param[1] = (u32)flash->buff;
		cmd.param[2] = to_write;
		cmd.param[3] = flash->clkrate / 1000;
		ret = lpc40xx_flash_iap(flash, &cmd);
		if (ret)
			return ret;

		count -= to_read;
		sptr += to_read;
		dptr += to_read;
		*retlen += to_read;
	}

	return 0;
}

static int lpc40xx_flash_read(struct mtd_info *mtd, loff_t from, size_t len,
			      size_t *retlen, u_char *buf)
{
	struct lpc40xx_flash *flash = mtd->priv;

	memcpy(buf, flash->base + from, len);
	*retlen = len;

	return 0;
}

static int __init lpc40xx_flash_getbootver(struct lpc40xx_flash *flash)
{
	struct iap_cmd cmd;
	int ret;

	cmd.cmd = IAP_READ_BOOT_VER;
	ret = lpc40xx_flash_iap(flash, &cmd);
	if (!ret)
		pr_info("IAP version %d.%d\n",
			(cmd.result[0] >> 8) & 0xff, cmd.result[0] & 0xff);

	return ret;
}

static int __init lpc40xx_flash_getpartid(struct lpc40xx_flash *flash)
{
	struct iap_cmd cmd;
	const char *part;
	int ret;

	cmd.cmd = IAP_READ_PART_ID;
	ret = lpc40xx_flash_iap(flash, &cmd);

	if (!ret) {
		switch (cmd.result[0]) {
		case 0x47011132:
			part = "LPC4074";
			flash->mtd.size = SZ_128K;
			break;
		case 0x47191f43:
			part = "LPC4076";
			flash->mtd.size = SZ_256K;
			break;
		case 0x47193f47:
			part = "LPC4078";
			flash->mtd.size = SZ_512K;
			break;
		case 0x481d3f47:
			part = "LPC4088";
			flash->mtd.size = SZ_512K;
			break;
		default:
			dev_info(flash->mtd.parent,
				 "Unknown part ID:0x%08x\n", cmd.result[0]);
			return -ENOTSUPP;
		}

		dev_dbg(flash->mtd.parent, "CPU part: %s\n", part);
	}

	return ret;
}

static int __init lpc40xx_flash_getserial(struct lpc40xx_flash *flash)
{
	struct iap_cmd cmd;
	int ret;

	cmd.cmd = IAP_READ_SERIAL_NUMBER;
	ret = lpc40xx_flash_iap(flash, &cmd);

	if (!ret) {
		char serstr[8 + 1];
		u32 crc;

		sprintf(serstr, "%08x", cmd.result[0]);
		setenv("SerialNumber0", serstr);
		sprintf(serstr, "%08x", cmd.result[1]);
		setenv("SerialNumber1", serstr);
		sprintf(serstr, "%08x", cmd.result[2]);
		setenv("SerialNumber2", serstr);
		sprintf(serstr, "%08x", cmd.result[3]);
		setenv("SerialNumber3", serstr);
		crc = crc32(0, &cmd.result, 4);
		sprintf(serstr, "%08x", crc);
		setenv("SerialCRC", serstr);
	}

	return ret;
}

static int __init lpc40xx_flash_probe(struct device_d *dev)
{
	struct lpc40xx_flash *flash;
	struct clk *clk = NULL;
	void __iomem *boot;
	int ret;

	flash = xzalloc(sizeof(*flash));
	dev->priv = (void *)flash;
	flash->mtd.parent = dev;
	flash->mtd.priv = flash;

	flash->base = dev_request_mem_region(dev, 0);
	if (IS_ERR(flash->base)) {
		ret = PTR_ERR(flash->base);
		goto out_err;
	}

	flash->buff = dev_request_mem_region(dev, 1);
	if (IS_ERR(flash->buff)) {
		ret = PTR_ERR(flash->buff);
		goto out_err;
	}

	boot = dev_request_mem_region(dev, 2);
	if (IS_ERR(boot)) {
		ret = PTR_ERR(boot);
		goto out_err;
	}

	clk = clk_get(dev, NULL);
	if (IS_ERR(clk)) {
		ret = PTR_ERR(clk);
		goto out_err;
	}

	flash->clkrate = clk_get_rate(clk);

	/* Setup IAP address */
	flash->iap = boot + IAP_OFFSET;

	ret = lpc40xx_flash_getbootver(flash);
	if (ret)
		goto out_err;

	ret = lpc40xx_flash_getpartid(flash);
	if (ret)
		goto out_err;
	ret = lpc40xx_flash_getserial(flash);
	if (ret)
		goto out_err;

	flash->mtd.type = MTD_ROM;
	flash->mtd.flags = MTD_WRITEABLE;
	flash->mtd.writesize = SZ_256;
	flash->mtd.erasesize = SZ_32K;

	flash->mtd.read = lpc40xx_flash_read;
	flash->mtd.write = lpc40xx_flash_write;
	flash->mtd.erase = lpc40xx_flash_erase;

	ret = add_mtd_device(&flash->mtd, "flash", DEVICE_ID_DYNAMIC);
	if (!ret)
		return 0;

out_err:
	if (clk)
		clk_put(clk);
	free(flash);

	return ret;
}

static struct driver_d lpc40xx_flash_driver = {
	.name 	= "lpc40xx-flash",
	.probe	= lpc40xx_flash_probe,
};
device_platform_driver(lpc40xx_flash_driver);
