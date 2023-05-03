// SPDX-License-Identifier: GPL-2.0-only

#define pr_fmt(fmt) "romapi: " fmt

#include <common.h>
#include <asm/sections.h>
#include <mach/romapi.h>
#include <mach/atf.h>
#include <mach/imx8m-regs.h>
#include <mach/xload.h>
#include <asm/barebox-arm.h>
#include <zero_page.h>
#include <pbl.h>

static int imx8m_bootrom_load(struct rom_api *rom_api, void *adr, size_t size)
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

/* read piggydata via a bootrom callback and place it behind our copy in SDRAM */
static int imx8m_bootrom_load_image(struct rom_api *rom_api)
{
	return imx8m_bootrom_load(rom_api,
				  (void *)MX8M_ATF_BL33_BASE_ADDR + barebox_pbl_size,
				  __image_end - __piggydata_start);
}

int imx8mp_bootrom_load_image(void)
{
	struct rom_api *rom_api = (void *)0x980;

	OPTIMIZER_HIDE_VAR(rom_api);

	return imx8m_bootrom_load_image(rom_api);
}

int imx8mn_bootrom_load_image(void)
{
	return imx8mp_bootrom_load_image();
}

const u32 *imx8m_get_bootrom_log(void)
{
	if (current_el() == 3) {
		ulong rom_log_addr;

		zero_page_access();
		rom_log_addr = readl(IOMEM(0x9e0));
		zero_page_faulting();

		if (rom_log_addr < MX8M_OCRAM_BASE_ADDR ||
		    rom_log_addr >= MX8M_OCRAM_BASE_ADDR + MX8M_OCRAM_MAX_SIZE ||
		    rom_log_addr & 0x3) {
			pr_warn("No BootROM log found at address 0x%08lx\n", rom_log_addr);
			return NULL;
		}

		return (u32 *)rom_log_addr;
	}

	if (!IN_PBL) {
		const struct imx_scratch_space *scratch = arm_mem_scratch_get();
		return scratch->bootrom_log;
	}

	return NULL;
}

void imx8m_save_bootrom_log(void *dest)
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

	pr_debug("Saving bootrom log to 0x%p\n", dest);

	memcpy(dest, rom_log, 128 * sizeof(u32));
}
