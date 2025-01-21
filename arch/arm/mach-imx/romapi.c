// SPDX-License-Identifier: GPL-2.0-only

#define pr_fmt(fmt) "romapi: " fmt

#include <common.h>
#include <linux/bitfield.h>
#include <soc/imx9/flash_header.h>
#include <asm/sections.h>
#include <mach/imx/romapi.h>
#include <mach/imx/atf.h>
#include <mach/imx/imx8m-regs.h>
#include <mach/imx/xload.h>
#include <mach/imx/generic.h>
#include <asm/barebox-arm.h>
#include <zero_page.h>
#include <memory.h>
#include <init.h>
#include <pbl.h>
#include <mmu.h>
#include <bootsource.h>

#define BOOTROM_INFO_VERSION		0x1
#define BOOTROM_INFO_BOOT_DEVICE	0x2
#define BOOTROM_INFO_DEVICE_PAGE_SIZE	0x3
#define BOOTROM_INFO_OFFSET_IVT		0x4
#define BOOTROM_INFO_BOOT_STAGE		0x5
#define BOOTROM_INFO_OFFSET_IMAGE	0x6

#define BOOTROM_BOOT_DEVICE_INTERFACE	GENMASK(23, 16)
#define BOOTROM_BOOT_DEVICE_INSTANCE	GENMASK(15, 8)
#define BOOTROM_BOOT_DEVICE_STATE	GENMASK(7, 0)

static int imx_bootrom_query(struct rom_api *rom_api, uint32_t type, uint32_t *__info)
{
	static uint32_t info;
	uint32_t xor = type ^ (uintptr_t)&info;
	int ret;

	ret = rom_api->query_boot_infor(type, &info, xor);
	if (ret != ROM_API_OKAY)
		return -EIO;

	*__info = info;

	return 0;
}

static int imx_romapi_load_stream(struct rom_api *rom_api, void *adr, size_t size)
{
	while (size) {
		size_t chunksize = min(size, (size_t)1024);
		int ret;

		ret = rom_api->download_image(adr, 0, chunksize,
					      (uintptr_t)adr ^ chunksize);
		if (ret != ROM_API_OKAY) {
			pr_err("Failed to load piggy data (ret = %x)\n", ret);
			return -EIO;
		}

		adr += chunksize;
		size -= chunksize;
	}
	return 0;
}

static int imx_romapi_load_seekable(struct rom_api *rom_api, void *adr, uint32_t offset,
				       size_t size)
{
	int ret;

	size = PAGE_ALIGN(size);

	ret = rom_api->download_image(adr, offset, size,
				      (uintptr_t)adr ^ offset ^ size);
	if (ret != ROM_API_OKAY) {
		pr_err("Failed to load piggy data (ret = %x)\n", ret);
		return -EIO;
	}

	return 0;
}

/* read piggydata via a bootrom callback and place it behind our copy in SDRAM */
static int imx_romapi_load_image(struct rom_api *rom_api, void *bl33)
{
	return imx_romapi_load_stream(rom_api, bl33 + barebox_pbl_size,
				      __image_end - __piggydata_start);
}

int imx8mp_romapi_load_image(void *bl33)
{
	struct rom_api *rom_api = (void *)0x980;

	OPTIMIZER_HIDE_VAR(rom_api);

	return imx_romapi_load_image(rom_api, bl33);
}

int imx8mn_romapi_load_image(void *bl33)
{
	return imx8mp_romapi_load_image(bl33);
}

static int imx_romapi_boot_device(struct rom_api *rom_api)
{
	uint32_t boot_device_type, boot_instance, boot_device;
	enum bootsource bootsource = BOOTSOURCE_UNKNOWN;
	int ret;

	ret = imx_bootrom_query(rom_api, BOOTROM_INFO_BOOT_DEVICE, &boot_device);
	if (ret)
		return ret;

	boot_device_type = FIELD_GET(BOOTROM_BOOT_DEVICE_INTERFACE, boot_device);
	boot_instance = FIELD_GET(BOOTROM_BOOT_DEVICE_INSTANCE, boot_device);

	switch (boot_device_type) {
	case BT_DEV_TYPE_MMC:
	case BT_DEV_TYPE_SD:
		bootsource = BOOTSOURCE_MMC;
		break;
	case BT_DEV_TYPE_NAND:
		bootsource = BOOTSOURCE_NAND;
		break;
	case BT_DEV_TYPE_FLEXSPINOR:
	case BT_DEV_TYPE_SPI_NOR:
		bootsource = BOOTSOURCE_SPI_NOR;
		break;
	case BT_DEV_TYPE_USB:
		bootsource = BOOTSOURCE_USB;
		break;
	}

	bootsource_set(bootsource, boot_instance);

	return 0;
}

static int imx_romapi_boot_device_seekable(struct rom_api *rom_api)
{
	uint32_t boot_device, boot_device_type, boot_device_state;
	int ret;
	bool seekable;

	ret = imx_bootrom_query(rom_api, BOOTROM_INFO_BOOT_DEVICE, &boot_device);
	if (ret)
		return ret;

	boot_device_type = FIELD_GET(BOOTROM_BOOT_DEVICE_INTERFACE, boot_device);

	switch (boot_device_type) {
	case BT_DEV_TYPE_SD:
	case BT_DEV_TYPE_NAND:
	case BT_DEV_TYPE_FLEXSPINOR:
	case BT_DEV_TYPE_SPI_NOR:
		seekable = true;
		break;
	case BT_DEV_TYPE_USB:
		seekable = false;
		break;
	case BT_DEV_TYPE_MMC:
		boot_device_state = FIELD_GET(BOOTROM_BOOT_DEVICE_STATE, boot_device);
		if (boot_device_state & BIT(0))
			seekable = false;
		else
			seekable = true;
		break;
	default:
		return -EINVAL;
	}

	return seekable;
}

int imx93_romapi_load_image(void *adr)
{
	struct rom_api *rom_api = (void *)0x1980;
	int ret;
	int seekable;
	uint32_t offset, image_offset;
	struct flash_header_v3 *fh;

	OPTIMIZER_HIDE_VAR(rom_api);

	seekable = imx_romapi_boot_device_seekable(rom_api);
	if (seekable < 0)
		return seekable;

	if (!seekable) {
		int align_size = ALIGN(barebox_pbl_size, 1024) - barebox_pbl_size;
		void *pbl_size_aligned = adr + ALIGN(barebox_pbl_size, 1024);

		/*
		 * The USB protocol uploads in chunks of 1024 bytes. This means
		 * the initial piggy data up to the next 1KiB boundary is already
		 * transferred. Align up the start address to this boundary.
		 */

		return imx_romapi_load_stream(rom_api,
				pbl_size_aligned,
				__image_end - __piggydata_start - align_size);
	}

	ret = imx_bootrom_query(rom_api, BOOTROM_INFO_OFFSET_IMAGE, &offset);
	if (ret)
		return ret;

	pr_debug("%s: IVT offset on boot device: 0x%08x\n", __func__, offset);

	ret = imx_romapi_load_seekable(rom_api, adr, offset, 4096);
	if (ret)
		return ret;

	fh = adr;

	if (fh->tag != 0x87) {
		pr_err("Invalid IVT header: 0x%02x, expected 0x87\n", fh->tag);
		return -EINVAL;
	}

	image_offset = fh->img[0].offset;

	pr_debug("%s: offset in image: 0x%08x\n", __func__, image_offset);

	/*
	 * We assume the first image in the first container is the barebox image,
	 * which is what the imx9image call in images/Makefile.imx generates.
	 */
	ret = imx_romapi_load_seekable(rom_api, adr, offset + image_offset, barebox_image_size);
	if (ret)
		return ret;

	return ret;
}

const u32 *imx8m_get_bootrom_log(void)
{
	if (current_el() == 3) {
		ulong *rom_log_addr_offset = (void *)0x9e0;
		ulong rom_log_addr;

		OPTIMIZER_HIDE_VAR(rom_log_addr_offset);

		zero_page_access();
		rom_log_addr = *rom_log_addr_offset;
		zero_page_faulting();

		if (rom_log_addr < MX8M_OCRAM_BASE_ADDR ||
		    rom_log_addr >= MX8M_OCRAM_BASE_ADDR + MX8M_OCRAM_MAX_SIZE ||
		    rom_log_addr & 0x3) {
			pr_warn("No BootROM log found at address 0x%08lx\n", rom_log_addr);
			return NULL;
		}

		return (u32 *)rom_log_addr;
	}

	if (IN_PROPER)
		return imx8m_scratch_get_bootrom_log();

	return NULL;
}

void imx8m_save_bootrom_log(void)
{
	const u32 *rom_log;

	if (!IS_ENABLED(CONFIG_IMX_SAVE_BOOTROM_LOG)) {
		pr_debug("skipping bootrom log saving\n");
		return;
	}

	rom_log = imx8m_get_bootrom_log();
	if (!rom_log) {
		pr_warn("bootrom log not found\n");
		return;
	}

	imx8m_scratch_save_bootrom_log(rom_log);
}

#define IMX93_BOOT_ROM_BASE 0x1000
#define IMX93_BOOT_ROM_END (0x40000 - 1)

void imx93_bootsource(void)
{
	struct rom_api *rom_api = (void *)0x1980;
	struct resource rom = {
		.start = IMX93_BOOT_ROM_BASE,
		.end = IMX93_BOOT_ROM_END,
	};
	struct resource *r;
	int ret;

	r = request_iomem_region("Boot ROM",  rom.start, rom.end);
	if (IS_ERR(r)) {
		ret = PTR_ERR(r);
		goto out;
	}

	arch_remap_range((void *)rom.start, rom.start, resource_size(&rom), MAP_CACHED);

	OPTIMIZER_HIDE_VAR(rom_api);

	ret = imx_romapi_boot_device(rom_api);
out:
	if (ret)
		pr_err("Failed to get bootsource: %pe\n", ERR_PTR(ret));
}
