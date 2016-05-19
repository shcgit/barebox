/*
 * MTD SPI driver for ST M25Pxx (and similar) serial flash chips
 *
 * Author: Mike Lavender, mike@steroidmicros.com
 * Copyright (c) 2005, Intec Automation Inc.
 *
 * Some parts are based on lart.c by Abraham Van Der Merwe
 *
 * Cleaned up and generalized based on mtd_dataflash.c
 *
 * This code is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <clock.h>
#include <common.h>
#include <driver.h>
#include <errno.h>
#include <init.h>
#include <io.h>
#include <malloc.h>
#include <of.h>
#include <xfuncs.h>
#include <spi/spi.h>
#include <spi/flash.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/math64.h>
#include <linux/mtd/cfi.h>
#include <linux/mtd/mtd.h>

/* Flash opcodes. */
#define	OPCODE_WREN		0x06	/* Write enable */
#define	OPCODE_RDSR		0x05	/* Read status register */
#define	OPCODE_WRSR		0x01	/* Write status register 1 byte */
#define	OPCODE_NORM_READ	0x03	/* Read data bytes (low frequency) */
#define	OPCODE_FAST_READ	0x0b	/* Read data bytes (high frequency) */
#define	OPCODE_PP		0x02	/* Page program (up to 256 bytes) */
#define	OPCODE_BE_4K		0x20	/* Erase 4KiB block */
#define	OPCODE_BE_32K		0x52	/* Erase 32KiB block */
#define	OPCODE_CHIP_ERASE	0xc7	/* Erase whole flash chip */
#define	OPCODE_SE		0xd8	/* Sector erase (usually 64KiB) */
#define	OPCODE_RDID		0x9f	/* Read JEDEC ID */

/* Used for SST flashes only. */
#define	OPCODE_BP		0x02	/* Byte program */
#define	OPCODE_WRDI		0x04	/* Write disable */
#define	OPCODE_AAI_WP		0xad	/* Auto address increment word program */

/* Used for Macronix flashes only. */
#define	OPCODE_EN4B		0xb7	/* Enter 4-byte mode */
#define	OPCODE_EX4B		0xe9	/* Exit 4-byte mode */

/* Used for Spansion flashes only. */
#define	OPCODE_BRWR		0x17	/* Bank register write */

/* Used for Macronix Quad flashes */
#define OPCODE_QREAD		0x6b

/* Status Register bits. */
#define	SR_WIP			BIT(0)	/* Write in progress */
#define	SR_WEL			BIT(1)	/* Write enable latch */
/* meaning of other SR_* bits may differ between vendors */
#define	SR_BP0			BIT(2)	/* Block protect 0 */
#define	SR_BP1			BIT(3)	/* Block protect 1 */
#define	SR_BP2			BIT(4)	/* Block protect 2 */
#define	SR_BP3			BIT(5)	/* Block protect 2 */
#define	SR_SRWD			BIT(7)	/* SR write protect */

/* Define max times to check status register before we give up. */
/* Micron N25Q128A specs 250s max chip erase */
#define	MAX_READY_WAIT		250
#define MAX_CMD_SIZE		6

#define JEDEC_MFR(_jedec_id)	((_jedec_id) >> 16)

/* Flags */
#define	FLAG_SECT_4K		BIT(0)	/* OPCODE_BE_4K works uniformly */
#define	FLAG_NO_ERASE		BIT(1)	/* No erase command needed */
#define FLAG_FAST_READ		BIT(2)

/****************************************************************************/

struct m25p {
	struct mtd_info	mtd;
	size_t		page_size;
	unsigned	sector_size;
	u16		addr_width;
	u8		flags;
	u32		jedec_id;
	u8		command[MAX_CMD_SIZE];
	int		(*tx_rx)(struct m25p *, unsigned, u8 *, unsigned);
	int		(*tx_tx)(struct m25p *, unsigned, const u8 *, unsigned);
};

static inline struct m25p *mtd_to_m25p(struct mtd_info *mtd)
{
	return container_of(mtd, struct m25p, mtd);
}

static int spi_tx_rx(struct m25p *flash, unsigned txlen, u8 *buf, unsigned len)
{
	struct spi_device *spi =
		container_of(flash->mtd.parent, struct spi_device, dev);

	return spi_write_then_read(spi, flash->command, txlen, buf, len);
}

static int spi_tx_tx(struct m25p *flash, unsigned tx1,
		     const u8 *buf2, unsigned tx2)
{
	struct spi_device *spi =
		container_of(flash->mtd.parent, struct spi_device, dev);
	struct spi_transfer t[2];
	struct spi_message m;

	spi_message_init(&m);
	memset(t, 0, (sizeof t));

	t[0].tx_buf = flash->command;
	t[0].len = tx1;
	t[1].tx_buf = buf2;
	t[1].len = tx2;
	spi_message_add_tail(&t[0], &m);
	spi_message_add_tail(&t[1], &m);

	return spi_sync(spi, &m);
}

static int m25p_cmd1(struct m25p *flash, u8 cmd)
{
	flash->command[0] = cmd;

	return flash->tx_rx(flash, 1, NULL, 0);
}

static int m25p_cmd2(struct m25p *flash, u8 cmd, u8 val)
{
	flash->command[0] = cmd;
	flash->command[1] = val;

	return flash->tx_rx(flash, 2, NULL, 0);
}

static int write_sr(struct m25p *flash, u8 val)
{
	return m25p_cmd2(flash, OPCODE_WRSR, val);
}

static int read_sr(struct m25p *flash)
{
	int ret;
	u8 sr;

	flash->command[0] = OPCODE_RDSR;
	ret = flash->tx_rx(flash, 1, &sr, 1);
	if (ret)
		return ret;

	return sr;
}

static int wait_sr(struct m25p *flash, u8 mask, u8 status, uint64_t timeout)
{
	uint64_t timer_start;
	int ret;

	timer_start = get_time_ns();

	do {
		ret = read_sr(flash);
		if (ret < 0) {
			dev_err(flash->mtd.parent, "error %i reading SR\n", ret);
			return ret;
		}
		if ((ret & mask) == status)
			return 0;

	} while (!(is_timeout(timer_start, timeout)));

	return -ETIMEDOUT;
}

static int wait_till_ready(struct m25p *flash)
{
	return wait_sr(flash, SR_WIP, 0, MAX_READY_WAIT * SECOND);
}

static int write_enable(struct m25p *flash)
{
	int ret;

	ret = m25p_cmd1(flash, OPCODE_WREN);
	if (ret < 0)
		return ret;

	return wait_sr(flash, SR_WEL, SR_WEL, 40 * MSECOND);
}

static inline int write_disable(struct m25p *flash)
{
	return m25p_cmd1(flash, OPCODE_WRDI);
}

/* Enable/disable 4-byte addressing mode */
static inline int set_4byte(struct m25p *flash, int enable)
{
	switch (flash->jedec_id) {
	case CFI_MFR_MACRONIX:
		return m25p_cmd1(flash, enable ? OPCODE_EN4B : OPCODE_EX4B);
	default:
		/* Spansion style */
		return m25p_cmd2(flash, OPCODE_BRWR, enable << 7);
	}
}

static int erase_chip(struct m25p *flash)
{
	int ret;

	dev_dbg(flash->mtd.parent, "%s %lldKiB\n",
		__func__, (long long)(flash->mtd.size >> 10));

	ret = write_enable(flash);
	if (ret)
		return ret;

	ret = m25p_cmd1(flash, OPCODE_CHIP_ERASE);
	if (ret)
		return ret;

	return wait_till_ready(flash);
}

static void m25p_addr2cmd(struct m25p *flash, u8 cmd, unsigned int addr)
{
	flash->command[0] = cmd;
	flash->command[1] = addr >> (flash->addr_width * 8 -  8);
	flash->command[2] = addr >> (flash->addr_width * 8 - 16);
	flash->command[3] = addr >> (flash->addr_width * 8 - 24);
	flash->command[4] = addr >> (flash->addr_width * 8 - 32);
}

static int m25p_cmdsz(struct m25p *flash)
{
	return 1 + flash->addr_width;
}

static int erase_sector(struct m25p *flash, u32 offset, u32 command)
{
	int ret;

	dev_dbg(flash->mtd.parent, "%s %dKiB at 0x%08x\n",
		__func__, flash->mtd.erasesize / 1024, offset);

	ret = write_enable(flash);
	if (ret)
		return ret;

	/* Set up command buffer. */
	m25p_addr2cmd(flash, command, offset);

	ret = flash->tx_rx(flash, m25p_cmdsz(flash), NULL, 0);
	if (ret)
		return ret;

	return wait_till_ready(flash);
}

static int m25p80_erase(struct mtd_info *mtd, struct erase_info *instr)
{
	struct m25p *flash = mtd_to_m25p(mtd);
	uint32_t rem;
	uint64_t len;
	u32 addr;
	int ret;

	dev_dbg(flash->mtd.parent, "%s at 0x%llx, len %lld\n", __func__,
		(long long)instr->addr, (long long)instr->len);

	div_u64_rem(instr->len, mtd->erasesize, &rem);
	if (rem)
		return -EINVAL;

	addr = instr->addr;
	len = instr->len;

	/* Whole-Chip erase? */
	if (len == flash->mtd.size) {
		ret = erase_chip(flash);
		if (ret) {
			instr->state = MTD_ERASE_FAILED;
			return ret;
		}
		goto erase_done;
	}

	if (flash->flags & FLAG_SECT_4K) {
		while (len && (addr & (flash->sector_size - 1))) {
			if (ctrlc())
				return -EINTR;
			ret = erase_sector(flash, addr, OPCODE_BE_4K);
			if (ret)
				return ret;
			addr += mtd->erasesize;
			len -= mtd->erasesize;
		}

		while (len >= flash->sector_size) {
			if (ctrlc())
				return -EINTR;
			ret = erase_sector(flash, addr, OPCODE_SE);
			if (ret)
				return ret;
			addr += flash->sector_size;
			len -= flash->sector_size;
		}

		while (len) {
			if (ctrlc())
				return -EINTR;
			ret = erase_sector(flash, addr, OPCODE_BE_4K);
			if (ret)
				return ret;
			addr += mtd->erasesize;
			len -= mtd->erasesize;
		}
	} else {
		while (len) {
			if (ctrlc())
				return -EINTR;
			ret = erase_sector(flash, addr, OPCODE_SE);
			if (ret)
				return ret;

			if (len <= mtd->erasesize)
				break;
			addr += mtd->erasesize;
			len -= mtd->erasesize;
		}
	}

erase_done:
	instr->state = MTD_ERASE_DONE;
	mtd_erase_callback(instr);

	return 0;
}

static int m25p80_read(struct mtd_info *mtd, loff_t from, size_t len,
		       size_t *retlen, u_char *buf)
{
	struct m25p *flash = mtd_to_m25p(mtd);
	int fast_read;

	fast_read = (flash->flags & FLAG_FAST_READ) ? 1 : 0;

	/* Set up the write data buffer. */
	m25p_addr2cmd(flash,
		      fast_read ? OPCODE_FAST_READ : OPCODE_NORM_READ, from);

	*retlen = len;

	return flash->tx_rx(flash, m25p_cmdsz(flash) + fast_read, buf, len);
}

static int m25p80_write(struct mtd_info *mtd, loff_t to, size_t len,
			size_t *retlen, const u_char *buf)
{
	struct m25p *flash = mtd_to_m25p(mtd);
	size_t i, to_write;
	int ret;

	to_write = flash->page_size - (to & (flash->page_size - 1));
	*retlen = 0;

	for (i = 0; i < len; ) {
		ret = write_enable(flash);
		if (ret)
			return ret;

		m25p_addr2cmd(flash, OPCODE_PP, to + i);
		ret = flash->tx_tx(flash, m25p_cmdsz(flash), buf + i, to_write);
		if (ret)
			return ret;

		dev_dbg(flash->mtd.parent, "%s %u bytes at 0x%08llx\n",
			__func__, to_write, to + i);

		*retlen += to_write;
		i += to_write;
		to_write = min(len - i, flash->page_size);

		ret = wait_till_ready(flash);
		if (ret)
			return ret;
	}

	return 0;
}

static int sst_write(struct mtd_info *mtd, loff_t to, size_t len,
		     size_t *retlen, const u_char *buf)
{
	struct m25p *flash = mtd_to_m25p(mtd);
	int cmd_sz, ret;
	size_t actual;

	dev_dbg(flash->mtd.parent, "%s to 0x%08x, len %zd\n",
		__func__, (u32)to, len);

	actual = to % 2;
	/* Start write from odd address. */
	if (actual) {
		ret = write_enable(flash);
		if (ret)
			return ret;

		/* write one byte. */
		m25p_addr2cmd(flash, OPCODE_BP, to);
		flash->tx_tx(flash, m25p_cmdsz(flash), buf, 1);
		ret = wait_till_ready(flash);
		if (ret)
			return ret;
		*retlen += 1;
		to += 1;
	}

	m25p_addr2cmd(flash, OPCODE_AAI_WP, to);

	/* Write out most of the data here. */
	cmd_sz = m25p_cmdsz(flash);
	for (; actual < len - 1; actual += 2) {
		ret = write_enable(flash);
		if (ret)
			return ret;

		m25p_addr2cmd(flash, OPCODE_AAI_WP, to);
		flash->tx_tx(flash, cmd_sz, buf + actual, 2);

		ret = wait_till_ready(flash);
		if (ret)
			return ret;
		*retlen += 2;
		cmd_sz = 1;
		to += 2;
	}

	/* Write out trailing byte if it exists. */
	if (actual != len) {
		ret = write_enable(flash);
		if (ret)
			return ret;

		m25p_addr2cmd(flash, OPCODE_BP, to);
		flash->tx_tx(flash, m25p_cmdsz(flash), buf + actual, 1);

		ret = wait_till_ready(flash);
		if (ret)
			return ret;

		*retlen += 1;
	}

	write_disable(flash);

	return ret;
}

struct flash_info {
	/* JEDEC id zero means "no ID" (most older chips); otherwise it has
	 * a high byte of zero plus three data bytes: the manufacturer id,
	 * then a two byte device id.
	 */
	u32	jedec_id;
	u16	ext_id;
	u32	ext9_id;

	/* The size listed here is what works with OPCODE_SE, which isn't
	 * necessarily called a "sector" by the vendor.
	 */
	size_t	sector_size;
	u16	n_sectors;

	size_t	page_size;
	u16	addr_width;

	u16	flags;
};

#define INFO(_jedec_id, _ext_id, _sector_size, _n_sectors, _flags)	\
	((unsigned long)&(struct flash_info) {				\
		.jedec_id = (_jedec_id),				\
		.ext_id = (_ext_id),					\
		.sector_size = (_sector_size),				\
		.n_sectors = (_n_sectors),				\
		.page_size = 256,					\
		.flags = (_flags),					\
	})

#define FM25_INFO(_jedec_id, _ext_id, _ext9_id, _flash_size)		\
	((unsigned long)&(struct flash_info) {				\
		.jedec_id = (_jedec_id),				\
		.ext_id = (_ext_id),					\
		.ext9_id = (_ext9_id),					\
		.sector_size = (_flash_size),				\
		.n_sectors = 1,						\
		.page_size = (_flash_size),				\
		.addr_width = ((_flash_size) > (64 * 1024)) ? 3 : 2,	\
		.flags = FLAG_NO_ERASE,					\
	})

#define CAT25_INFO(_sector_size, _n_sectors, _page_size, _addr_width)	\
	((unsigned long)&(struct flash_info) {				\
		.sector_size = (_sector_size),				\
		.n_sectors = (_n_sectors),				\
		.page_size = (_page_size),				\
		.addr_width = (_addr_width),				\
		.flags = FLAG_NO_ERASE,					\
	})

/* NOTE: double check command sets and memory organization when you add
 * more flash chips.  This current list focusses on newer chips, which
 * have been converging on command sets which including JEDEC ID.
 */
static const struct platform_device_id m25p_ids[] = {
	/* Atmel -- some are (confusingly) marketed as "DataFlash" */
	{ "at25fs010",		INFO(0x1f6601, 0, 32 * 1024,   4, FLAG_SECT_4K) },
	{ "at25fs040",		INFO(0x1f6604, 0, 64 * 1024,   8, FLAG_SECT_4K) },

	{ "at25df041a",		INFO(0x1f4401, 0, 64 * 1024,   8, FLAG_SECT_4K) },
	{ "at25df321a",		INFO(0x1f4701, 0, 64 * 1024,  64, FLAG_SECT_4K) },
	{ "at25df641",		INFO(0x1f4800, 0, 64 * 1024, 128, FLAG_SECT_4K) },

	{ "at26f004",		INFO(0x1f0400, 0, 64 * 1024,  8, FLAG_SECT_4K) },
	{ "at26df081a",		INFO(0x1f4501, 0, 64 * 1024, 16, FLAG_SECT_4K) },
	{ "at26df161a",		INFO(0x1f4601, 0, 64 * 1024, 32, FLAG_SECT_4K) },
	{ "at26df321",		INFO(0x1f4700, 0, 64 * 1024, 64, FLAG_SECT_4K) },

	{ "at45db081d",		INFO(0x1f2500, 0, 64 * 1024, 16, FLAG_SECT_4K) },

	/* Cypress (Ramtron) */
	{ "fm25cl64-nonjedec",	FM25_INFO(       0,      0,          0,   8 * 1024) },
	{ "fm25v01",		FM25_INFO(0x7f7f7f, 0x7f7f, 0x7fc22100,  16 * 1024) },
	{ "fm25v01a",		FM25_INFO(0x7f7f7f, 0x7f7f, 0x7fc22108,  16 * 1024) },
	{ "fm25v02",		FM25_INFO(0x7f7f7f, 0x7f7f, 0x7fc22200,  32 * 1024) },
	{ "fm25v02a",		FM25_INFO(0x7f7f7f, 0x7f7f, 0x7fc22208,  32 * 1024) },
	{ "fm25v05",		FM25_INFO(0x7f7f7f, 0x7f7f, 0x7fc22300,  64 * 1024) },
	{ "fm25v10",		FM25_INFO(0x7f7f7f, 0x7f7f, 0x7fc22400, 128 * 1024) },
	{ "fm25v20",		FM25_INFO(0x7f7f7f, 0x7f7f, 0x7fc22500, 256 * 1024) },
	{ "fm25v20a",		FM25_INFO(0x7f7f7f, 0x7f7f, 0x7fc22508, 256 * 1024) },

	/* EON -- en25xxx */
	{ "en25f32",		INFO(0x1c3116, 0, 64 * 1024,  64, FLAG_SECT_4K) },
	{ "en25p32",		INFO(0x1c2016, 0, 64 * 1024,  64, 0) },
	{ "en25q32b",		INFO(0x1c3016, 0, 64 * 1024,  64, 0) },
	{ "en25p64",		INFO(0x1c2017, 0, 64 * 1024, 128, 0) },
	{ "en25q64",		INFO(0x1c3017, 0, 64 * 1024, 128, FLAG_SECT_4K) },

	/* Everspin */
	{ "mr25h256",		CAT25_INFO(32 * 1024, 1, 256, 2) },

	/* Intel/Numonyx -- xxxs33b */
	{ "160s33b",		INFO(0x898911, 0, 64 * 1024,  32, 0) },
	{ "320s33b",		INFO(0x898912, 0, 64 * 1024,  64, 0) },
	{ "640s33b",		INFO(0x898913, 0, 64 * 1024, 128, 0) },
	{ "n25q064",		INFO(0x20ba17, 0, 64 * 1024, 128, 0) },

	/* Macronix */
	{ "mx25l2005a",		INFO(0xc22012, 0, 64 * 1024,   4, FLAG_SECT_4K) },
	{ "mx25l4005a",		INFO(0xc22013, 0, 64 * 1024,   8, FLAG_SECT_4K) },
	{ "mx25l8005",		INFO(0xc22014, 0, 64 * 1024,  16, 0) },
	{ "mx25l1606e",		INFO(0xc22015, 0, 64 * 1024,  32, FLAG_SECT_4K) },
	{ "mx25l3205d",		INFO(0xc22016, 0, 64 * 1024,  64, 0) },
	{ "mx25l6405d",		INFO(0xc22017, 0, 64 * 1024, 128, 0) },
	{ "mx25l12805d",	INFO(0xc22018, 0, 64 * 1024, 256, 0) },
	{ "mx25l12855e",	INFO(0xc22618, 0, 64 * 1024, 256, 0) },
	{ "mx25l25635e",	INFO(0xc22019, 0, 64 * 1024, 512, 0) },
	{ "mx25l25655e",	INFO(0xc22619, 0, 64 * 1024, 512, 0) },

	/* Micron */
	{ "n25q064",		INFO(0x20ba17, 0, 64 * 1024,  128, 0) },
	{ "n25q128a11",		INFO(0x20bb18, 0, 64 * 1024,  256, 0) },
	{ "n25q128a13",		INFO(0x20ba18, 0, 64 * 1024,  256, 0) },
	{ "n25q256a",		INFO(0x20ba19, 0, 64 * 1024,  512, FLAG_SECT_4K) },

	/* Spansion -- single (large) sector size only, at least
	 * for the chips listed here (without boot sectors).
	 */
	{ "s25sl032p",		INFO(0x010215, 0x4d00,  64 * 1024,  64, 0) },
	{ "s25sl064p",		INFO(0x010216, 0x4d00,  64 * 1024, 128, 0) },
	{ "s25fl256s0",		INFO(0x010219, 0x4d00, 256 * 1024, 128, 0) },
	{ "s25fl256s1",		INFO(0x010219, 0x4d01,  64 * 1024, 512, 0) },
	{ "s25fl512s",		INFO(0x010220, 0x4d00, 256 * 1024, 256, 0) },
	{ "s70fl01gs",		INFO(0x010221, 0x4d00, 256 * 1024, 256, 0) },
	{ "s25sl12800",		INFO(0x012018, 0x0300, 256 * 1024,  64, 0) },
	{ "s25sl12801",		INFO(0x012018, 0x0301,  64 * 1024, 256, 0) },
	{ "s25fl129p0",		INFO(0x012018, 0x4d00, 256 * 1024,  64, 0) },
	{ "s25fl129p1",		INFO(0x012018, 0x4d01,  64 * 1024, 256, 0) },
	{ "s25sl004a",		INFO(0x010212,      0,  64 * 1024,   8, 0) },
	{ "s25sl008a",		INFO(0x010213,      0,  64 * 1024,  16, 0) },
	{ "s25sl016a",		INFO(0x010214,      0,  64 * 1024,  32, 0) },
	{ "s25sl032a",		INFO(0x010215,      0,  64 * 1024,  64, 0) },
	{ "s25sl064a",		INFO(0x010216,      0,  64 * 1024, 128, 0) },
	{ "s25fl016k",		INFO(0xef4015,      0,  64 * 1024,  32, FLAG_SECT_4K) },
	{ "s25fl064k",		INFO(0xef4017,      0,  64 * 1024, 128, FLAG_SECT_4K) },

	/* SST -- large erase sizes are "overlays", "sectors" are 4K */
	{ "sst25vf040b",	INFO(0xbf258d, 0, 64 * 1024,  8, FLAG_SECT_4K) },
	{ "sst25vf080b",	INFO(0xbf258e, 0, 64 * 1024, 16, FLAG_SECT_4K) },
	{ "sst25vf016b",	INFO(0xbf2541, 0, 64 * 1024, 32, FLAG_SECT_4K) },
	{ "sst25vf032b",	INFO(0xbf254a, 0, 64 * 1024, 64, FLAG_SECT_4K) },
	{ "sst25wf512",		INFO(0xbf2501, 0, 64 * 1024,  1, FLAG_SECT_4K) },
	{ "sst25wf010",		INFO(0xbf2502, 0, 64 * 1024,  2, FLAG_SECT_4K) },
	{ "sst25wf020", 	INFO(0xbf2503, 0, 64 * 1024,  4, FLAG_SECT_4K) },
	{ "sst25wf040",		INFO(0xbf2504, 0, 64 * 1024,  8, FLAG_SECT_4K) },

	/* ST Microelectronics -- newer production may have feature updates */
	{ "m25p05",		INFO(0x202010,  0,  32 * 1024,   2, 0) },
	{ "m25p10",		INFO(0x202011,  0,  32 * 1024,   4, 0) },
	{ "m25p20",		INFO(0x202012,  0,  64 * 1024,   4, 0) },
	{ "m25p40",		INFO(0x202013,  0,  64 * 1024,   8, 0) },
	{ "m25p80",		INFO(0x202014,  0,  64 * 1024,  16, 0) },
	{ "m25p16",		INFO(0x202015,  0,  64 * 1024,  32, 0) },
	{ "m25p32",		INFO(0x202016,  0,  64 * 1024,  64, 0) },
	{ "m25p64",		INFO(0x202017,  0,  64 * 1024, 128, 0) },
	{ "m25p128",		INFO(0x202018,  0, 256 * 1024,  64, 0) },
	{ "n25q032",		INFO(0x20ba16,  0,  64 * 1024,  64, 0) },

	{ "m25p05-nonjedec",	INFO(0, 0,  32 * 1024,   2, 0) },
	{ "m25p10-nonjedec",	INFO(0, 0,  32 * 1024,   4, 0) },
	{ "m25p20-nonjedec",	INFO(0, 0,  64 * 1024,   4, 0) },
	{ "m25p40-nonjedec",	INFO(0, 0,  64 * 1024,   8, 0) },
	{ "m25p80-nonjedec",	INFO(0, 0,  64 * 1024,  16, 0) },
	{ "m25p16-nonjedec",	INFO(0, 0,  64 * 1024,  32, 0) },
	{ "m25p32-nonjedec",	INFO(0, 0,  64 * 1024,  64, 0) },
	{ "m25p64-nonjedec",	INFO(0, 0,  64 * 1024, 128, 0) },
	{ "m25p128-nonjedec",	INFO(0, 0, 256 * 1024,  64, 0) },

	{ "m45pe10",		INFO(0x204011,  0, 64 * 1024,    2, 0) },
	{ "m45pe80",		INFO(0x204014,  0, 64 * 1024,   16, 0) },
	{ "m45pe16",		INFO(0x204015,  0, 64 * 1024,   32, 0) },

	{ "m25pe20",		INFO(0x208012,  0, 64 * 1024,  4,       0) },
	{ "m25pe80",		INFO(0x208014,  0, 64 * 1024, 16,       0) },
	{ "m25pe16",		INFO(0x208015,  0, 64 * 1024, 32, FLAG_SECT_4K) },

	{ "m25px32",		INFO(0x207116,  0, 64 * 1024, 64, FLAG_SECT_4K) },
	{ "m25px32-s0",		INFO(0x207316,  0, 64 * 1024, 64, FLAG_SECT_4K) },
	{ "m25px32-s1",		INFO(0x206316,  0, 64 * 1024, 64, FLAG_SECT_4K) },
	{ "m25px64",		INFO(0x207117,  0, 64 * 1024, 128, 0) },

	/* Winbond -- w25x "blocks" are 64K, "sectors" are 4KiB */
	{ "w25x10",		INFO(0xef3011, 0, 64 * 1024,  2,  FLAG_SECT_4K) },
	{ "w25x20",		INFO(0xef3012, 0, 64 * 1024,  4,  FLAG_SECT_4K) },
	{ "w25x40",		INFO(0xef3013, 0, 64 * 1024,  8,  FLAG_SECT_4K) },
	{ "w25x80",		INFO(0xef3014, 0, 64 * 1024,  16, FLAG_SECT_4K) },
	{ "w25x16",		INFO(0xef3015, 0, 64 * 1024,  32, FLAG_SECT_4K) },
	{ "w25x32",		INFO(0xef3016, 0, 64 * 1024,  64, FLAG_SECT_4K) },
	{ "w25q32",		INFO(0xef4016, 0, 64 * 1024,  64, FLAG_SECT_4K) },
	{ "w25q32dw",		INFO(0xef6016, 0, 64 * 1024,  64, FLAG_SECT_4K) },
	{ "w25x64",		INFO(0xef3017, 0, 64 * 1024, 128, FLAG_SECT_4K) },
	{ "w25q64",		INFO(0xef4017, 0, 64 * 1024, 128, FLAG_SECT_4K) },
	{ "w25q80",		INFO(0xef5014, 0, 64 * 1024,  16, FLAG_SECT_4K) },
	{ "w25q80bl",		INFO(0xef4014, 0, 64 * 1024,  16, FLAG_SECT_4K) },

	/* Catalyst / On Semiconductor -- non-JEDEC */
	{ "cat25c11",		CAT25_INFO(  16, 8, 16, 1) },
	{ "cat25c03",		CAT25_INFO(  32, 8, 16, 2) },
	{ "cat25c09",		CAT25_INFO( 128, 8, 32, 2) },
	{ "cat25c17",		CAT25_INFO( 256, 8, 32, 2) },
	{ "cat25128",		CAT25_INFO(2048, 8, 64, 2) },

	{ },
};

static const struct platform_device_id *jedec_probe(struct device_d *dev)
{
	struct m25p *flash = dev->priv;
	struct flash_info *info;
	u32 jedec, ext9_jedec;
	u16 ext_jedec;
	u8 id[9];
	int tmp;

	/* JEDEC also defines an optional "extended device information"
	 * string for after vendor-specific data, after the three bytes
	 * we use here.  Supporting some chips might require using it.
	 */
	flash->command[0] = OPCODE_RDID;
	tmp = flash->tx_rx(flash, 1, id, sizeof(id));
	if (tmp < 0) {
		dev_dbg(dev, "%s: Error %d reading JEDEC ID\n",
			dev_name(dev), tmp);
		return ERR_PTR(tmp);
	}

	jedec = (id[0] << 16) | (id[1] << 8) | id[2];
	ext_jedec = (id[3] << 8) | id[4];
	ext9_jedec = (id[5] << 24) | (id[6] << 16) | (id[7] << 8) | id[8];

	for (tmp = 0; tmp < ARRAY_SIZE(m25p_ids) - 1; tmp++) {
		info = (void *)m25p_ids[tmp].driver_data;
		if (info->jedec_id && (info->jedec_id == jedec)) {
			if (info->ext_id && (info->ext_id != ext_jedec))
				continue;
			if (info->ext9_id && (info->ext9_id != ext9_jedec))
				continue;
			return &m25p_ids[tmp];
		}
	}

	dev_err(dev, "Unrecognized JEDEC id %06x:%04x:%08x\n",
		jedec, ext_jedec, ext9_jedec);

	return ERR_PTR(-ENODEV);
}

/*
 * board specific setup should have ensured the SPI clock used here
 * matches what the READ command supports, at least until this driver
 * understands FAST_READ (for clocks over 25 MHz).
 */
static int m25p_probe(struct device_d *dev)
{
	const struct platform_device_id	*id = NULL;
	struct flash_platform_data *data;
	struct m25p *flash = dev->priv;
	struct flash_info *info = NULL;
	unsigned i, do_jdec_probe = 1;
	const char *typename = NULL;
	char *flashname = NULL;
	int device_id;

	/* Platform data helps sort out which chip type we have, as
	 * well as how this board partitions it.  If we don't have
	 * a chip ID, try the JEDEC id commands; they'll work for most
	 * newer chips, even if we don't recognize the particular chip.
	 */
	data = dev->platform_data;
	if (data && data->type)
		typename = data->type;
	else if (dev->id_entry)
		typename = dev->id_entry->name;

	if (typename) {
		const struct platform_device_id *plat_id;

		for (i = 0; i < ARRAY_SIZE(m25p_ids) - 1; i++) {
			plat_id = &m25p_ids[i];
			if (strcmp(typename, plat_id->name))
				continue;
			break;
		}

		if (i < ARRAY_SIZE(m25p_ids) - 1) {
			id = plat_id;
			info = (void *)id->driver_data;
			/* If flash type is provided but the memory is not
			 * JEDEC compliant, don't try to probe the JEDEC id */
			if (!info->jedec_id)
				do_jdec_probe = 0;
		} else
			dev_warn(dev, "Unrecognized id %s\n", typename);
	}

	if (do_jdec_probe) {
		const struct platform_device_id *jid;

		jid = jedec_probe(dev);
		if (IS_ERR(jid)) {
			return PTR_ERR(jid);
		} else if (jid != id) {
			/*
			 * JEDEC knows better, so overwrite platform ID. We
			 * can't trust partitions any longer, but we'll let
			 * mtd apply them anyway, since some partitions may be
			 * marked read-only, and we don't want to lose that
			 * information, even if it's not 100% accurate.
			 */
			if (id)
				dev_warn(dev, "Found %s, Expected %s\n",
					 jid->name, id->name);

			id = jid;
			info = (void *)jid->driver_data;
		}
	}

	flash->jedec_id = JEDEC_MFR(info->jedec_id);
	/*
	 * Atmel, SST and Intel/Numonyx serial flash tend to power
	 * up with the software protection bits set
	 */

	if (flash->jedec_id == CFI_MFR_ATMEL ||
	    flash->jedec_id == CFI_MFR_INTEL ||
	    flash->jedec_id == CFI_MFR_SST) {
		write_enable(flash);
		write_sr(flash, 0);
	}

	device_id = DEVICE_ID_SINGLE;
	if (dev->device_node) {
		const char *alias = of_alias_get(dev->device_node);
		if (alias)
			flashname = xstrdup(alias);
	} else if (data && data->name) {
		flashname = data->name;
	}

	if (!flashname) {
		device_id = DEVICE_ID_DYNAMIC;
		flashname = "m25p";
	}

	flash->mtd.type = MTD_NORFLASH;
	flash->mtd.writesize = 1;
	flash->mtd.flags = MTD_CAP_NORFLASH;
	flash->mtd.size = (uint64_t)info->sector_size * info->n_sectors;
	flash->mtd.erase = m25p80_erase;
	flash->mtd.read = m25p80_read;

	/* SST flash chips use AAI word program */
	if (flash->jedec_id == CFI_MFR_SST)
		flash->mtd.write = sst_write;
	else
		flash->mtd.write = m25p80_write;

	flash->flags |= info->flags;

	/* prefer "small sector" erase if possible */
	if (info->flags & FLAG_SECT_4K)
		flash->mtd.erasesize = 4096;
	else
		flash->mtd.erasesize = info->sector_size;

	if (info->flags & FLAG_NO_ERASE)
		flash->mtd.flags |= MTD_NO_ERASE;

	flash->page_size = info->page_size;
	flash->sector_size = info->sector_size;

	if (info->addr_width) {
		flash->addr_width = info->addr_width;
	} else {
		/* enable 4-byte addressing if the device exceeds 16MiB */
		if (flash->mtd.size > 0x1000000) {
			flash->addr_width = 4;
			set_4byte(flash, 1);
		} else
			flash->addr_width = 3;
	}

	dev_info(dev, "%s (%lld Kbytes)\n", id->name,
		 (long long)flash->mtd.size >> 10);

	dev_dbg(dev, "mtd .name = %s, .size = 0x%llx (%lldMiB) "
		".erasesize = 0x%.8x (%uKiB) .numeraseregions = %d\n",
		flash->mtd.name,
		(long long)flash->mtd.size, (long long)(flash->mtd.size >> 20),
		flash->mtd.erasesize, flash->mtd.erasesize / 1024,
		flash->mtd.numeraseregions);

	if (flash->mtd.numeraseregions)
		for (i = 0; i < flash->mtd.numeraseregions; i++)
			dev_dbg(dev, "mtd.eraseregions[%d] = { .offset = 0x%llx, "
				".erasesize = 0x%.8x (%uKiB), "
				".numblocks = %d }\n",
				i, (long long)flash->mtd.eraseregions[i].offset,
				flash->mtd.eraseregions[i].erasesize,
				flash->mtd.eraseregions[i].erasesize / 1024,
				flash->mtd.eraseregions[i].numblocks);

	return add_mtd_device(&flash->mtd, flashname, device_id);
}

static int __init m25p_spi_probe(struct device_d *dev)
{
	struct spi_device *spi = (struct spi_device *)dev->type_data;
	struct m25p *flash;
	int ret;

	flash = xzalloc(sizeof(*flash));
	dev->priv = (void *)flash;
	flash->mtd.parent = &spi->dev;

	flash->tx_rx = spi_tx_rx;
	flash->tx_tx = spi_tx_tx;

	flash->flags = (spi->max_speed_hz >= 25000000) ? FLAG_FAST_READ : 0;

	ret = m25p_probe(dev);
	if (ret)
		free(flash);

	return ret;
}

static __maybe_unused struct of_device_id m25p80_dt_ids[] = {
	{ .compatible	= "m25p80", },
	{ }
};

static struct driver_d m25p80_driver = {
	.name		= "m25p80",
	.probe		= m25p_spi_probe,
	.of_compatible	= DRV_OF_COMPAT(m25p80_dt_ids),
	.id_table	= (struct platform_device_id *)m25p_ids,
};
device_spi_driver(m25p80_driver);

#define SPIFI_CTRL		(0x00)
# define CTRL_TIMEOUT(x)	(min((x), 0xffff))
# define CTRL_CSHIGH(x)		(min(((x) ? ((x) - 1) : 0), 0xf) << 16)
# define CTRL_FBCLK		BIT(30)
#define SPIFI_CMD		(0x04)
#define SPIFI_ADDR		(0x08)
#define SPIFI_IDATA		(0x0c)
#define SPIFI_CLIMIT		(0x10)
#define SPIFI_DATA		(0x14)
#define SPIFI_MCMD		(0x18)
#define SPIFI_STAT		(0x1c)
# define STAT_MCINIT		BIT(0)
# define STAT_CMD		BIT(1)
# define STAT_RESET		BIT(4)

struct lpc40xx_spifi {
	struct m25p	flash;
	struct clk	*clk;
	void __iomem	*regs;
	void __iomem	*map;
	unsigned char	quad;
	int		(*quad_enable)(struct m25p *, unsigned);
};

static int spifi_tx_rx(struct m25p *flash, unsigned txlen, u8 *buf, unsigned len)
{
	struct lpc40xx_spifi *spifi =
		container_of(flash, struct lpc40xx_spifi, flash);
	u32 i, tmp;

	if (txlen > 1) {
		for (i = 1, tmp = 0; i < txlen; i++) {
			tmp <<= 8;
			tmp |= flash->command[i];
		}
		writel(tmp, spifi->regs + SPIFI_ADDR);
	}

	tmp = flash->command[0] << 24;
	tmp |= txlen << 21;
	tmp |= 0 << 19;
	tmp |= 0 << 16;
	tmp |= len;
	writel(tmp, spifi->regs + SPIFI_CMD);

	if (buf)
		readsb(spifi->regs + SPIFI_DATA, buf, len);

	do {
	} while (readl(spifi->regs + SPIFI_STAT) & STAT_CMD);

	return 0;
}

static int spifi_tx_tx(struct m25p *flash, unsigned tx1,
		     const u8 *buf2, unsigned tx2)
{
	struct lpc40xx_spifi *spifi =
		container_of(flash, struct lpc40xx_spifi, flash);
	u32 i, tmp;

	if (tx1 > 1) {
		for (i = 1, tmp = 0; i < tx1; i++) {
			tmp <<= 8;
			tmp |= flash->command[i];
		}
		writel(tmp, spifi->regs + SPIFI_ADDR);
	}

	tmp = flash->command[0] << 24;
	tmp |= tx1 << 21;
	tmp |= 0 << 19;
	tmp |= 0 << 16;
	tmp |= 1 << 15;
	tmp |= tx2;
	writel(tmp, spifi->regs + SPIFI_CMD);

	if (buf2)
		writesb(spifi->regs + SPIFI_DATA, buf2, tx2);

	do {
	} while (readl(spifi->regs + SPIFI_STAT) & STAT_CMD);

	return 0;
}

int quad_enable_macronix(struct m25p *flash, unsigned enable)
{
	int ret;

	ret = write_enable(flash);
	if (ret < 0)
		return ret;

	write_sr(flash, enable ? BIT(6) : 0);

	ret = wait_sr(flash, SR_WIP, 0, 40 * MSECOND);

	ret = read_sr(flash);
	if (ret < 0)
		return ret;

	return ((ret & BIT(6)) == (enable ? BIT(6) : 0)) ? 0 : -EIO;
}

int quad_enable_other(struct m25p *flash, unsigned enable)
{
	int ret;

	ret = write_enable(flash);
	if (ret < 0)
		return ret;

	flash->command[0] = OPCODE_WRSR;
	flash->command[1] = 0;
	flash->command[2] = enable ? BIT(2) : 0;
	flash->tx_rx(flash, 3, NULL, 0);

	return 0;
}

static int spifi_mem_mode(struct lpc40xx_spifi *spifi, unsigned mode)
{
	u32 tmp = readl(spifi->regs + SPIFI_STAT) & STAT_MCINIT;
	int ret = 0;

	if (mode && !tmp) {
		ret = spifi->quad_enable(&spifi->flash, 1);
		if (ret)
			return ret;
		/* Enable Memory Mode */
		tmp = OPCODE_QREAD << 24;
		tmp |= m25p_cmdsz(&spifi->flash) << 21;
		tmp |= 1 << 19; /* 1-1-4 */
		tmp |= 1 << 16; /* Dummy */
		writel(tmp, spifi->regs + SPIFI_MCMD);
		do {
		} while (!(readl(spifi->regs + SPIFI_STAT) & STAT_MCINIT));
		writel(spifi->flash.mtd.size, spifi->regs + SPIFI_CLIMIT);
	} else if (tmp && !mode) {
		/* Cancel Memory Mode */
		writel(STAT_RESET, spifi->regs + SPIFI_STAT);
		do {
		} while (readl(spifi->regs + SPIFI_STAT) & STAT_RESET);

		ret = spifi->quad_enable(&spifi->flash, 0);
	}

	return ret;
}

static int spifi_erase(struct mtd_info *mtd, struct erase_info *instr)
{
	struct m25p *flash = mtd_to_m25p(mtd);
	struct lpc40xx_spifi *spifi =
		container_of(flash, struct lpc40xx_spifi, flash);
	int ret;

	ret = spifi_mem_mode(spifi, 0);
	if (ret)
		return ret;
	ret = m25p80_erase(mtd, instr);
	spifi_mem_mode(spifi, 1);

	return ret;
}

static int spifi_write(struct mtd_info *mtd, loff_t to, size_t len,
		       size_t *retlen, const u_char *buf)
{
	struct m25p *flash = mtd_to_m25p(mtd);
	struct lpc40xx_spifi *spifi =
		container_of(flash, struct lpc40xx_spifi, flash);
	int ret;

	ret = spifi_mem_mode(spifi, 0);
	if (ret)
		return ret;
	ret = m25p80_write(mtd, to, len, retlen, buf);
	spifi_mem_mode(spifi, 1);

	return ret;
}

static int spifi_read(struct mtd_info *mtd, loff_t from, size_t len,
		      size_t *retlen, u_char *buf)
{
	struct m25p *flash = mtd_to_m25p(mtd);
	struct lpc40xx_spifi *spifi =
		container_of(flash, struct lpc40xx_spifi, flash);

	memcpy(buf, spifi->map + from, len);
	*retlen = len;

	return 0;
}

static int __init m25p_spifi_probe(struct device_d *dev)
{
	struct lpc40xx_spifi *spifi;
	int ret;

	spifi = xzalloc(sizeof(*spifi));
	dev->priv = &spifi->flash;
	spifi->flash.mtd.parent = dev;

	spifi->regs = dev_request_mem_region(dev, 0);
	if (IS_ERR(spifi->regs)) {
		ret = PTR_ERR(spifi->regs);
		goto out_err;
	}

	spifi->map = dev_request_mem_region(dev, 1);
	if (IS_ERR(spifi->map)) {
		ret = PTR_ERR(spifi->map);
		goto out_err;
	}

	spifi->clk = clk_get(dev, NULL);
	if (IS_ERR(spifi->clk)) {
		ret = PTR_ERR(spifi->clk);
		goto out_err;
	}

	spifi->flash.tx_rx = spifi_tx_rx;
	spifi->flash.tx_tx = spifi_tx_tx;
	spifi->flash.flags = FLAG_FAST_READ;

	/* SPIFI Maximum clock rate for READ (1 Dummy cycle) is 43MHz */
	clk_set_rate(spifi->clk, 43000000);

	/* Setup */
	ret = spifi_mem_mode(spifi, 0);
	if (ret)
		goto out_err;

	writel(0, spifi->regs + SPIFI_IDATA);
	writel(CTRL_TIMEOUT(0xffff) | CTRL_CSHIGH(4) | CTRL_FBCLK,
	       spifi->regs + SPIFI_CTRL);

	ret = m25p_probe(dev);
	if (ret) {
		clk_put(spifi->clk);
		goto out_err;
	}

	if (spifi->flash.jedec_id == CFI_MFR_MACRONIX)
		spifi->quad_enable = quad_enable_macronix;
	else
		spifi->quad_enable = quad_enable_other;

	ret = spifi_mem_mode(spifi, 1);
	if (ret) {
		del_mtd_device(&spifi->flash.mtd);
		goto out_err;
	}

	spifi->flash.mtd.read = spifi_read;
	spifi->flash.mtd.write = spifi_write;
	spifi->flash.mtd.erase = spifi_erase;

	return 0;

out_err:
	free(spifi);

	return ret;
}

static struct driver_d lpc40xx_spifi_driver = {
	.name 	= "lpc40xx-spifi",
	.probe	= m25p_spifi_probe,
};
device_platform_driver(lpc40xx_spifi_driver);
