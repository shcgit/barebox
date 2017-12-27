/*
 * Copyright (C) 2015 Alexander Shiyan <shc_work@mail.ru>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 */

#include <command.h>
#include <common.h>
#include <crc.h>
#include <errno.h>
#include <fs.h>
#include <fcntl.h>
#include <init.h>
#include <io.h>
#include <i2c/i2c.h>
#include <linux/sizes.h>
#include <linux/stat.h>

#include <mach/hardware.h>

#define CF_IMAGE_MAGIC			0x12345678
#define CF_IMAGE_VERSION		1
#define PART_TYPE_BOOT			(1 << 0)
#define PART_TYPE_KERNEL		(1 << 1)
#define PART_TYPE_ROOT			(1 << 2)
#define ATA_SECTOR_SIZE			512
#define HW_MACHINE_ID_LPC4078_VX(x)	(4096 + (x))
#define BUFFER_SIZE			4096

struct cf_kernel_data_v1 {
	u32	magic;
	u32	structsize;
	u32	machine_id;
	u32	version;
	u32	incremented_version;
	u32	parts;
	u32	applied_version;
	u32	startsector;
	u32	next_sector;
	u32	size;
	u32	crc;
} __attribute__((__packed__));

static const char *flashname = "/dev/flash0";
static const char *spifiname = "/dev/m25p0";
static unsigned char buff[BUFFER_SIZE];

static int flash_size(const char *name)
{
	struct stat s;

	if (stat(name, &s)) {
		printf("STAT %s failed: %s\n", name, errno_str());
		return -1;
	}

	return s.st_size;
}

static int flash_erase(const char *name, int size)
{
	int fd, ret;

	fd = open(name, O_WRONLY);
	if (fd < 0)
		return fd;

	ret = erase(fd, size, 0);

	close(fd);

	return ret;
}

static int get_hwrev(void)
{
	struct i2c_adapter *adapter = NULL;
	struct i2c_client client;
	u8 buf;

	adapter = i2c_get_adapter(0);
	if (!adapter) {
		printf("\e[1;31mI2C bus 0 not found.\e[0m\n");
		return -1;
	}

	client.adapter = adapter;
	client.addr = 0x41;
	if (i2c_read_reg(&client, 0, &buf, 1) == 1)
		return buf & 0xf;

	return -1;
}

static int do_mmupdate(int argc, char *argv[])
{
	int flashsize, spifisize, hw, fdin = -1, fdout = -1, ret = 0;
	struct cf_kernel_data_v1 data;
	u32 version, crc, crcout;
	loff_t pos, to_read;
	char *file;

	flashsize = flash_size(flashname);
	if (flashsize <= 0)
		return -EIO;

	version = readl(LPC4088_FLASH + flashsize - 4);
	if (!version || (version == 0xffffffff)) {
		printf("\e[1;31mInformer version is not set.\e[0m\n");
		version = 0;
	} else
		printf("Informer version:\t\e[1;33m%u\e[0m\n", version);

	if (argc < 2) {
		printf("\e[1;31mUpdate file is not set.\e[0m\n");
		return -EINVAL;
	}

	hw = get_hwrev();
	if (hw < 0) {
		printf("HW-revision is set to \e[1;37m0\e[0m\n");
		hw = 0;
	} else
		printf("HW-revision:\t\t\e[1;37m%i\e[0m\n", hw);

	file = argv[1];

	fdin = open(file, O_RDONLY);
	if (fdin < 0) {
		printf("OPEN %s: %s\n", file, errno_str());
		return fdin;
	}

	for (;;) {
		if (read(fdin, &data, sizeof(data)) != sizeof(data)) {
			printf("READ %s error.\n", file);
			ret = -EIO;
			goto out_err;
		}

		if ((data.magic != CF_IMAGE_MAGIC) ||
		    (data.structsize != sizeof(data)) ||
		    (data.version != CF_IMAGE_VERSION)) {
			printf("\e[1;31mUpdate %s is incorrect.\e[0m\n", file);
			ret = 0;
			goto out_err;
		}

		if ((data.machine_id != HW_MACHINE_ID_LPC4078_VX(hw)) ||
		    (data.incremented_version <= version) ||
		    (data.applied_version && (version != data.applied_version))) {
			if (!data.next_sector) {
				printf("\e[1;33mNo valid updates found.\e[0m\n");
				ret = 0;
				goto out_err;
			} else {
				lseek(fdin, data.next_sector * ATA_SECTOR_SIZE, SEEK_SET);
				continue;
			}
		}

		printf("New version:\t\t\e[1;33m%u\e[0m\n", data.incremented_version);
		pos = data.startsector * ATA_SECTOR_SIZE;
		to_read = data.size;

		lseek(fdin, pos, SEEK_SET);
		crc = 0xffffffff;

		printf("Checking CRC...\t\t");

		while (to_read) {
			loff_t i, tmp;

			tmp = (to_read > sizeof(buff)) ? sizeof(buff) : to_read;
			if (read(fdin, buff, tmp) != tmp) {
				printf("\e[1;31mFAIL\e[0m\nREAD %s error.\n", file);
				ret = -EIO;
				goto out_err;
			}

			for (i = 0; i < tmp; i++)
				buff[i] = ~buff[i];

			crc = crc32_no_comp(crc, buff, tmp);
			to_read -= tmp;
		}

		if (crc != data.crc) {
			printf("\e[1;31mFAIL\e[0m\n");
			ret = -EIO;
			goto out_err;
		}

		printf("\e[1;32mOK\e[0m\n");

		if (data.parts & PART_TYPE_BOOT) {
			printf("Erasing flash...\t");

			ret = flash_erase(flashname, flashsize);
			if (ret) {
				printf("\e[1;31mFAIL\e[0m\n");
				goto out_err;
			}

			printf("\e[1;32mOK\e[0m\n");

			fdout = open(flashname, O_WRONLY);
			if (fdout < 0) {
				printf("\e[1;31Can't open flash for write.\e[0m\n");
				ret = fdout;
				goto out_err;
			}

			to_read = flashsize;

			lseek(fdin, pos, SEEK_SET);
			crc = 0xffffffff;

			printf("Flashing bootloader...\t");

			while (to_read) {
				loff_t i, tmp;

				tmp = (to_read > sizeof(buff)) ? sizeof(buff) : to_read;
				if (read(fdin, buff, tmp) != tmp) {
					printf("\e[1;31mFAIL\e[0m\nREAD error.\n");
					ret = -EIO;
					goto out_err;
				}

				for (i = 0; i < tmp; i++)
					buff[i] = ~buff[i];

				if (write(fdout, buff, tmp) != tmp) {
					printf("\e[1;31mFAIL\e[0m\nWRITE error.\n");
					ret = -EIO;
					goto out_err;
				}

				crc = crc32_no_comp(crc, buff, tmp);
				to_read -= tmp;
			}

			printf("\e[1;32mOK\e[0m\n");

			close(fdout);

			fdout = open(flashname, O_RDONLY);
			if (fdout < 0) {
				printf("\e[1;31Can't open flash for read.\e[0m\n");
				ret = fdout;
				goto out_err;
			}

			to_read = flashsize;

			crcout = 0xffffffff;

			printf("Checking CRC...\t\t");

			while (to_read) {
				loff_t tmp;

				tmp = (to_read > sizeof(buff)) ? sizeof(buff) : to_read;
				if (read(fdout, buff, tmp) != tmp) {
					printf("\e[1;31mFAIL\e[0m\nREAD error.\n");
					ret = -EIO;
					goto out_err;
				}

				crcout = crc32_no_comp(crcout, buff, tmp);
				to_read -= tmp;
			}

			close(fdout);

			if (crc != crcout) {
				printf("\e[1;31mFAIL\e[0m\n");
				ret = -EIO;
				goto out_err;
			}

			printf("\e[1;32mOK\e[0m\n");
		}

		if (data.parts & PART_TYPE_ROOT) {
			spifisize = flash_size(spifiname);
			if (spifisize <= 0) {
				ret = -EIO;
				goto out_err;
			}

			printf("Erasing SPIFI...\t");

			ret = flash_erase(spifiname, spifisize);
			if (ret) {
				printf("\e[1;31mFAIL\e[0m\n");
				goto out_err;
			}

			printf("\e[1;32mOK\e[0m\n");

			fdout = open(spifiname, O_WRONLY);
			if (fdout < 0) {
				printf("\e[1;31Can't open flash for write.\e[0m\n");
				ret = fdout;
				goto out_err;
			}

			data.size -= 4 * 1024 * 1024;

			to_read = (data.size < spifisize) ? data.size : spifisize;

			lseek(fdin, pos + (4 * 1024 * 1024), SEEK_SET);
			crc = 0xffffffff;
			printf("Flashing SPIFI...\t");

			while (to_read) {
				loff_t i, tmp;

				tmp = (to_read > sizeof(buff)) ? sizeof(buff) : to_read;
				if (read(fdin, buff, tmp) != tmp) {
					printf("\e[1;31mFAIL\e[0m\nREAD error.\n");
					ret = -EIO;
					goto out_err;
				}

				for (i = 0; i < tmp; i++)
					buff[i] = ~buff[i];

				if (write(fdout, buff, tmp) != tmp) {
					printf("\e[1;31mFAIL\e[0m\nWRITE error.\n");
					ret = -EIO;
					goto out_err;
				}

				crc = crc32_no_comp(crc, buff, tmp);
				to_read -= tmp;
			}

			printf("\e[1;32mOK\e[0m\n");

			close(fdout);

			fdout = open(spifiname, O_RDONLY);
			if (fdout < 0) {
				printf("\e[1;31Can't open flash for read.\e[0m\n");
				ret = fdout;
				goto out_err;
			}

			to_read = (data.size < spifisize) ? data.size : spifisize;

			crcout = 0xffffffff;

			printf("Checking CRC...\t\t");

			while (to_read) {
				loff_t tmp;

				tmp = (to_read > sizeof(buff)) ? sizeof(buff) : to_read;
				if (read(fdout, buff, tmp) != tmp) {
					printf("\e[1;31mFAIL\e[0m\nREAD error.\n");
					ret = -EIO;
					goto out_err;
				}

				crcout = crc32_no_comp(crcout, buff, tmp);
				to_read -= tmp;
			}

			close(fdout);

			if (crc != crcout) {
				printf("\e[1;31mFAIL\e[0m\n");
				ret = -EIO;
				goto out_err;
			}

			printf("\e[1;32mOK\e[0m\n");
			
			ret = 73;
			goto out_err;
		}

		break;
	}

	ret = 0;

out_err:
	if (fdout > -1)
		close(fdout);

	if (fdin > -1)
		close(fdin);

	return ret;
}

BAREBOX_CMD_START(mmupdate)
	.cmd	= do_mmupdate,
BAREBOX_CMD_END

static int do_mmstart(int argc, char *argv[])
{
	u32 addr = readl(LPC4088_SPIFI_BUFFER + 4);
	int (*func)(int, char *[]) = (void *)addr;

	if ((addr > LPC4088_SPIFI_BUFFER) && (addr < (LPC4088_SPIFI_BUFFER + SZ_8K))) {
		console_flush();
		shutdown_barebox();
		func(argc - 1, &argv[1]);
		hang();
	} else
		printf("SPIFI start address is not set.\n");

	return 0;
}

BAREBOX_CMD_START(mmstart)
	.cmd	= do_mmstart,
BAREBOX_CMD_END

static ssize_t serial_write(struct cdev *cdev, const void *buf, size_t count,
			    loff_t offset, ulong flags)
{
	struct console_device *con = console_get_by_dev(cdev->priv);
	const char *data = buf;
	size_t cnt = count;

	while (cnt--)
		con->putc(con, *data++);

	return count;
}

static struct file_operations serialops = {
	.write = serial_write,
	.lseek = dev_lseek_default,
};

static int serial1_init(void)
{
	struct cdev *cdev = xzalloc(sizeof (*cdev));
	struct device_d *dev = lpc4088_add_uart(1);
	struct console_device *con = console_get_by_dev(dev);

	con->setbrg(con, 19200);

	cdev->priv = dev;
	cdev->name = "serial1";
	cdev->flags = DEVFS_IS_CHARACTER_DEV;
	cdev->ops = &serialops;

	return devfs_create(cdev);
}
late_initcall(serial1_init);
