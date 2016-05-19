/*
 * Copyright (C) 2015 Alexander Shiyan <shc_work@mail.ru>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 */

#include <common.h>
#include <init.h>
#include <mci.h>
#include <io.h>
#include <malloc.h>
#include <clock.h>
#include <linux/clk.h>
#include <linux/err.h>

//#include "sdhci.h"

#define MCI_PWR				(0x00)
# define MCI_PWR_UP			(2 << 0)
# define MCI_PWR_ON			(3 << 0)
# define MCI_PWR_OPENDRAIN		(1 << 6)
#define MCI_CLOCK			(0x04)
# define MCI_CLOCK_ENABLE		(1 << 8)
# define MCI_CLOCK_PWRSAVE		(1 << 9)
# define MCI_CLOCK_WIDEBUS		(1 << 11)
#define MCI_ARGUMENT			(0x08)
#define MCI_COMMAND			(0x0c)
# define MCI_COMMAND_RESPONSE		(1 << 6)
# define MCI_COMMAND_LONGRESP		(1 << 7)
# define MCI_COMMAND_ENABLE		(1 << 10)
#define MCI_RESPCMD			(0x10)
#define MCI_RESPONSE0			(0x14)
#define MCI_RESPONSE1			(0x18)
#define MCI_RESPONSE2			(0x1c)
#define MCI_RESPONSE3			(0x20)
#define MCI_DATATIMER			(0x24)
#define MCI_DATALENGTH			(0x28)
#define MCI_DATACTRL			(0x2c)
# define MCI_DATACTRL_ENABLE		(1 << 0)
# define MCI_DATACTRL_DIRECTION		(1 << 1)
# define MCI_DATACTRL_BLOCK(x)		((x) << 4)
#define MCI_DATACOUNT			(0x30)
#define MCI_STATUS			(0x34)
# define MCI_STATUS_CMDCRCFAIL		(1 << 0)
# define MCI_STATUS_DATACRCFAIL		(1 << 1)
# define MCI_STATUS_CMDTIMEOUT		(1 << 2)
# define MCI_STATUS_DATATIMEOUT		(1 << 3)
# define MCI_STATUS_TXUNDERRUN		(1 << 4)
# define MCI_STATUS_RXOVERRUN		(1 << 5)
# define MCI_STATUS_CMDRESPEND		(1 << 6)
# define MCI_STATUS_CMDSENT		(1 << 7)
# define MCI_STATUS_DATAEND		(1 << 8)
# define MCI_STATUS_STARTBITERR		(1 << 9)
# define MCI_STATUS_DATABLOCKEND	(1 << 10)
# define MCI_STATUS_CMDACTIVE		(1 << 11)
# define MCI_STATUS_TXACTIVE		(1 << 12)
# define MCI_STATUS_RXACTIVE		(1 << 13)
# define MCI_STATUS_TXFIFOHALFEMPTY	(1 << 14)
# define MCI_STATUS_RXFIFOHALFFULL	(1 << 15)
# define MCI_STATUS_TXFIFOFULL		(1 << 16)
# define MCI_STATUS_RXFIFOFULL		(1 << 17)
# define MCI_STATUS_TXFIFOEMPTY		(1 << 18)
# define MCI_STATUS_RXFIFOEMPTY		(1 << 19)
# define MCI_STATUS_TXDATAAVLBL		(1 << 20)
# define MCI_STATUS_RXDATAAVLBL		(1 << 21)
#define MCI_CLEAR			(0x38)
# define MCI_CLEAR_CMDCRCFAIL		(1 << 0)
# define MCI_CLEAR_DATACRCFAIL		(1 << 1)
# define MCI_CLEAR_CMDTIMEOUT		(1 << 2)
# define MCI_CLEAR_DATATIMEOUT		(1 << 3)
# define MCI_CLEAR_TXUNDERRUN		(1 << 4)
# define MCI_CLEAR_RXOVERRUN		(1 << 5)
# define MCI_CLEAR_CMDRESPEND		(1 << 6)
# define MCI_CLEAR_CMDSENT		(1 << 7)
# define MCI_CLEAR_DATAEND		(1 << 8)
# define MCI_CLEAR_STARTBITERR		(1 << 9)
# define MCI_CLEAR_DATABLOCKEND		(1 << 10)
# define MCI_CLEAR_ALL			(0x7ff)
#define MCI_MASK0			(0x3c)

#define MCI_FIFOCNT			(0x48)
#define MCI_FIFO			(0x80)

struct lpc40xx_mci_host {
	struct mci_host	mci;
	struct clk	*clk;
	void __iomem	*regs;

	struct device_d *dev;

	unsigned	clock;
	unsigned char	bw;
	unsigned long	barrier_ns;
};

static void lpc40xx_mci_write_barrier(struct lpc40xx_mci_host *host, u32 value,
				      u32 addr)
{
	writel(value, host->regs + addr);
	volatile u8 i;
	for (i=0;i<0x10;i++);
	//ndelay(host->barrier_ns);
}

static int lpc40xx_mci_wait(struct lpc40xx_mci_host *host, struct mci_cmd *cmd)
{
	u32 hoststatus, statusmask;

	statusmask = MCI_STATUS_CMDTIMEOUT | MCI_STATUS_CMDCRCFAIL;
	if ((cmd->resp_type & MMC_RSP_PRESENT))
		statusmask |= MCI_STATUS_CMDRESPEND;
	else
		statusmask |= MCI_STATUS_CMDSENT;

	do {
		hoststatus = readl(host->regs + MCI_STATUS);
	} while (!(hoststatus & statusmask));

	writel(statusmask, host->regs + MCI_CLEAR);

	if (hoststatus & MCI_STATUS_CMDTIMEOUT) {
		pr_info("CMD%d time out\n", cmd->cmdidx);
		return -ETIMEDOUT;
	} else if ((hoststatus & MCI_STATUS_CMDCRCFAIL) &&
		   (cmd->resp_type & MMC_RSP_CRC)) {
		pr_info("CMD%d CRC error\n", cmd->cmdidx);
		return -EILSEQ;
	}

	if (cmd->resp_type & MMC_RSP_PRESENT) {
		cmd->response[0] = readl(host->regs + MCI_RESPONSE0);
		cmd->response[1] = readl(host->regs + MCI_RESPONSE1);
		cmd->response[2] = readl(host->regs + MCI_RESPONSE2);
		cmd->response[3] = readl(host->regs + MCI_RESPONSE3);
	}

	return 0;
}

static int lpc40xx_mci_command(struct lpc40xx_mci_host *host,
			       struct mci_cmd *cmd)
{
	u32 ret, command = cmd->cmdidx | MCI_COMMAND_ENABLE;

	if (cmd->resp_type) {
		command |= MCI_COMMAND_RESPONSE;
		if (cmd->resp_type & MMC_RSP_136)
			command |= MCI_COMMAND_LONGRESP;
	}

	/* Clear status */
	writel(MCI_CLEAR_ALL, host->regs + MCI_CLEAR);
	/* Start command */
	writel(cmd->cmdarg, host->regs + MCI_ARGUMENT);

	lpc40xx_mci_write_barrier(host, command, MCI_COMMAND);

	ret = lpc40xx_mci_wait(host, cmd);

	/* After CMD3 open drain is switched off and push pull is used. */
//	if ((ret == 0) && (cmd->cmdidx == MMC_CMD_SET_RELATIVE_ADDR)) {
//		u32 pwr = readl(host->regs + MCI_PWR) & ~MCI_PWR_OPENDRAIN;
//		lpc40xx_mci_write_barrier(host, pwr, MCI_PWR);
//	}

	return ret;
}

static int lpc40xx_mci_read_blk(struct lpc40xx_mci_host *host, char *buffer,
				unsigned int remain)
{
	char *ptr = buffer;

	do {
		u32 count, status = readl(host->regs + MCI_STATUS);

		if (status & MCI_STATUS_RXFIFOHALFFULL)
			count = 8 * 4; /* Half size */
		else if (status & MCI_STATUS_RXDATAAVLBL)
			count = 1 * 4; /* Just One Word */
		else
			break;

		count = min(count, remain);

		if (unlikely(count & 0x3)) {
			if (count < 4) {
				unsigned char buf[4];

				readsl(host->regs + MCI_FIFO, buf, 1);
				memcpy(ptr, buf, count);
			} else {
				readsl(host->regs + MCI_FIFO, ptr, count >> 2);
				count &= ~0x3;
			}
		} else
			readsl(host->regs + MCI_FIFO, ptr, count >> 2);

		ptr += count;
		remain -= count;
	} while (remain);

	return ptr - buffer;
}

static int lpc40xx_mci_readarray(struct lpc40xx_mci_host *host, char *dest,
				 unsigned int blkcount, unsigned int blksize)
{
	unsigned int xfercount = blkcount * blksize;
	u32 status, status_err;

	status_err = MCI_STATUS_DATACRCFAIL | MCI_STATUS_DATATIMEOUT |
		     MCI_STATUS_RXOVERRUN;
	do {
		int len = lpc40xx_mci_read_blk(host, dest, xfercount);
		xfercount -= len;
		dest += len;
		status = readl(host->regs + MCI_STATUS);
	} while (xfercount && !(status & status_err));

	status_err = MCI_STATUS_DATACRCFAIL | MCI_STATUS_DATATIMEOUT |
		     MCI_STATUS_DATABLOCKEND | MCI_STATUS_RXOVERRUN;

	while (!(status & status_err))
		status = readl(host->regs + MCI_STATUS);

	writel(MCI_CLEAR_ALL, host->regs + MCI_CLEAR);

	if (status & MCI_STATUS_DATATIMEOUT) {
		pr_info("Read data timed out, xfercount: %u, status: 0x%08X\n",
			xfercount, status);
		return -ETIMEDOUT;
	} else if (status & MCI_STATUS_DATACRCFAIL) {
		pr_info("Read data bytes CRC error: 0x%x\n", status);
		return -EILSEQ;
	} else if (status & MCI_STATUS_RXOVERRUN) {
		pr_info("Read data RX overflow error\n");
		return -EIO;
	}

	if (xfercount) {
		pr_info("Read data error, xfercount: %u\n", xfercount);
		return -ENOBUFS;
	}

	return 0;
}

static int lpc40xx_mci_write_blk(struct lpc40xx_mci_host *host, char *buffer,
				 unsigned int remain)
{
	char *ptr = buffer;

	do {
		u32 count, status = readl(host->regs + MCI_STATUS);
		if (status & MCI_STATUS_TXFIFOEMPTY)
			count = 16 * 4; /* Full size */
		else if (status & MCI_STATUS_TXFIFOHALFEMPTY)
			count = 8 * 4; /* Half size */
		else
			break;

		count = min(count, remain);

		writesl(host->regs + MCI_FIFO, ptr, (count + 3) >> 2);

		ptr += count;
		remain -= count;
	} while (remain);

	return ptr - buffer;
}

static int lpc40xx_mci_writearray(struct lpc40xx_mci_host *host, char *dest,
				  unsigned int blkcount, unsigned int blksize)
{
	unsigned int xfercount = blkcount * blksize;
	u32 status, status_err;

	status_err = MCI_STATUS_DATACRCFAIL | MCI_STATUS_DATATIMEOUT |
		     MCI_STATUS_TXUNDERRUN;
	do {
		int len = lpc40xx_mci_write_blk(host, dest, xfercount);
		xfercount -= len;
		dest += len;
		status = readl(host->regs + MCI_STATUS);
	} while (xfercount && !(status & status_err));

	status_err = MCI_STATUS_DATACRCFAIL | MCI_STATUS_DATATIMEOUT |
		     MCI_STATUS_DATABLOCKEND | MCI_STATUS_TXUNDERRUN;

	while (!(status & status_err))
		status = readl(host->regs + MCI_STATUS);

	writel(MCI_CLEAR_ALL, host->regs + MCI_CLEAR);

	if (status & MCI_STATUS_DATATIMEOUT) {
		pr_info("Write data timed out, xfercount:%u,status:0x%08X\n",
			xfercount, status);
		return -ETIMEDOUT;
	} else if (status & MCI_STATUS_DATACRCFAIL) {
		pr_info("Write data CRC error\n");
		return -EILSEQ;
	} else if (status & MCI_STATUS_TXUNDERRUN) {
		pr_info("Write data TX underrun error\n");
		return -EIO;
	}

	if (xfercount) {
		pr_info("Write data error, xfercount:%u", xfercount);
		return -ENOBUFS;
	}

	return -1;
}

static int lpc40xx_mci_request(struct mci_host *mci, struct mci_cmd *cmd,
			       struct mci_data *data)
{
	struct lpc40xx_mci_host *host =
		container_of(mci, struct lpc40xx_mci_host, mci);
	int ret;

pr_info("CMD%d\n",cmd->cmdidx);
udelay(30);
	if (data) {
		u32 data_len = (u32)(data->blocks * data->blocksize);
		u32 blksz_bits = ffs(data->blocksize) - 1;
		u32 data_ctrl = MCI_DATACTRL_BLOCK(blksz_bits);

		ret = lpc40xx_mci_command(host, cmd);
		if (ret)
			return ret;

		writel(data_len, host->regs + MCI_DATALENGTH);
		if (data->flags & MMC_DATA_READ)
			data_ctrl |= MCI_DATACTRL_DIRECTION;
		writel(data_ctrl | MCI_DATACTRL_ENABLE, host->regs + MCI_DATACTRL);

		if (data->flags & MMC_DATA_READ)
			ret = lpc40xx_mci_readarray(host, data->dest,
						    data->blocks,
						    data->blocksize);
		else if (data->flags & MMC_DATA_WRITE)
			ret = lpc40xx_mci_writearray(host, (char *)data->src,
						     data->blocks,
						     data->blocksize);
	} else {
		ret = lpc40xx_mci_command(host, cmd);
	}

	return ret;
}

static void lpc40xx_mci_set_ios(struct mci_host *mci, struct mci_ios *ios)
{
	struct lpc40xx_mci_host *host =
		container_of(mci, struct lpc40xx_mci_host, mci);
	u32 val = 0, reg = readl(host->regs + MCI_CLOCK);
	unsigned long rate = 0;

	if (ios->clock) {
		unsigned long clk = clk_get_rate(host->clk);
		val = DIV_ROUND_UP(clk / 2, ios->clock);
		if (val)
			val--;
		rate = clk / (2 * (val + 1));
		while (ios->bus_width == MMC_BUS_WIDTH_4) {
			if (rate < 15000000)
				break;
			val++;
			rate = clk / (2 * (val + 1));
		}
		val |= MCI_CLOCK_ENABLE/* | MCI_CLOCK_PWRSAVE*/;
	}

	if (ios->bus_width == MMC_BUS_WIDTH_4)
		val |= MCI_CLOCK_WIDEBUS;

	if (val != reg) {
		lpc40xx_mci_write_barrier(host, val, MCI_CLOCK);
		pr_info("IO settings: Bus width %d, Frequency %lu Hz\n",
			1 << ios->bus_width, rate);
	}
}

int lpc40xx_mci_init(struct mci_host *mci, struct device_d *mci_dev)
{
	struct lpc40xx_mci_host *host =
		container_of(mci, struct lpc40xx_mci_host, mci);

	/* Disable clock */
	lpc40xx_mci_write_barrier(host, 0, MCI_CLOCK);
	/* Power off */
	lpc40xx_mci_write_barrier(host, MCI_PWR_OPENDRAIN, MCI_PWR);
	/* Disable all interrupts */
	writel(0, host->regs + MCI_MASK0);
	/* Setting for timeout */
	writel(0x1fffffff, host->regs + MCI_DATATIMER);
	/* Clear any registers as needed */
	lpc40xx_mci_write_barrier(host, 0, MCI_COMMAND);
	lpc40xx_mci_write_barrier(host, 0, MCI_DATACTRL);
	/* Clear status */
	writel(MCI_CLEAR_ALL, host->regs + MCI_CLEAR);
	/* Power up */
	lpc40xx_mci_write_barrier(host, MCI_PWR_UP, MCI_PWR);
	mdelay(250);
	/* Power on */
	lpc40xx_mci_write_barrier(host, MCI_PWR_ON, MCI_PWR);

	return 0;
}

//static int lpc40xx_mci_detect(struct device_d *dev)
//{
//	struct lpc40xx_mci_host *host = dev->priv;
//
//	return mci_detect_card(&host->mci);
//}

static int lpc40xx_mci_probe(struct device_d *dev)
{
	struct lpc40xx_mci_host *host;
	unsigned long rate;
	int ret;

	host = xzalloc(sizeof(*host));

	host->clk = clk_get(dev, NULL);
	if (IS_ERR(host->clk)) {
		ret = PTR_ERR(host->clk);
		goto out_err;
	}

	ret = clk_enable(host->clk);
	if (ret) {
		clk_put(host->clk);
		goto out_err;
	}

	host->regs = dev_request_mem_region(dev, 0);
	if (IS_ERR(host->regs)) {
		ret =PTR_ERR(host->regs);
		goto out_err;
	}

	host->mci.init = lpc40xx_mci_init;
	host->mci.set_ios = lpc40xx_mci_set_ios;
	host->mci.send_cmd = lpc40xx_mci_request;

	rate = clk_get_rate(host->clk);
	host->mci.f_max = rate / (2 * (0 + 1));
	host->mci.f_min = rate / (2 * (255 + 1));
	host->mci.max_req_size = (1 << 16) - 1;

	/*
	 * After a data write, data cannot be written to some registers for
	 * three MCLK clock perios plus two PCLK clock periods.
	 * We use 3.5 PCLK clock cycles.
	 */
	host->barrier_ns = DIV_ROUND_UP(3500000, rate / 1000);

	host->mci.hw_dev = dev;
	host->dev = dev;

	host->mci.host_caps = MMC_CAP_4_BIT_DATA | MMC_CAP_SD_HIGHSPEED |
			      MMC_CAP_MMC_HIGHSPEED;
	host->mci.voltages = MMC_VDD_32_33 | MMC_VDD_33_34;

	dev->priv = host;

//	dev->detect = lpc40xx_mci_detect,

	ret = mci_register(&host->mci);
	if (!ret)
		return 0;

	clk_put(host->clk);

out_err:
	if (ret)
		free(host);

	return ret;
}

static struct driver_d lpc40xx_mci_driver = {
	.name	= "lpc40xx-mci",
	.probe	= lpc40xx_mci_probe,
};
device_platform_driver(lpc40xx_mci_driver);
