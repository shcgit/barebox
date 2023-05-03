/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef __MACH_XLOAD_H
#define __MACH_XLOAD_H

#include <linux/compiler.h>
#include <linux/types.h>

int imx53_nand_start_image(void);
int imx6_spi_load_image(int instance, unsigned int flash_offset, void *buf, int len);
int imx6_spi_start_image(int instance);
int imx6_esdhc_start_image(int instance);
int imx6_nand_start_image(void);
int imx7_esdhc_start_image(int instance);
int imx8m_esdhc_load_image(int instance, bool start);
int imx8mn_esdhc_load_image(int instance, bool start);
int imx8mp_esdhc_load_image(int instance, bool start);

void imx8mm_load_bl33(void *bl33);
void imx8mn_load_bl33(void *bl33);
void imx8mp_load_bl33(void *bl33);

void __noreturn imx8mm_load_and_start_image_via_tfa(void);
void __noreturn imx8mn_load_and_start_image_via_tfa(void);
void __noreturn imx8mp_load_and_start_image_via_tfa(void);

int imx_image_size(void);
int piggydata_size(void);

extern unsigned char input_data[];
extern unsigned char input_data_end[];

#endif /* __MACH_XLOAD_H */
