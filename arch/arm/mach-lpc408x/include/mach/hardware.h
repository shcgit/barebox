/*
 * Copyright (C) 2014 Alexander Shiyan <shc_work@mail.ru>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 */

#ifndef __MACH_HARDWARE_H
#define __MACH_HARDWARE_H

#define LPC4088_FLASH		(0x00000000)

#define LPC4088_SRAM		(0x10000000)
#define LPC4088_BOOTROM		(0x1fff0000)

#define LPC4088_AHB		(0x20080000)
#define LPC4088_GPDMA		(LPC4088_AHB + 0x00000000)
#define LPC4088_USB		(LPC4088_AHB + 0x0000c000)
#define LPC4088_SPIFI		(LPC4088_AHB + 0x00014000)
#define LPC4088_GPIO		(LPC4088_AHB + 0x00018000)
#define LPC4088_EMC		(LPC4088_AHB + 0x0001c000)

#define LPC4088_SPIFI_BUFFER	(0x28000000)

#define LPC4088_APB0		(0x40000000)
#define LPC4088_WDT		(LPC4088_APB0 + 0x00000000)
#define LPC4088_TIMER0		(LPC4088_APB0 + 0x00004000)
#define LPC4088_UART0		(LPC4088_APB0 + 0x0000c000)
#define LPC4088_UART1		(LPC4088_APB0 + 0x00010000)
#define LPC4088_I2C0		(LPC4088_APB0 + 0x0001c000)
#define LPC4088_PINCTRL		(LPC4088_APB0 + 0x0002c000)
#define LPC4088_I2C1		(LPC4088_APB0 + 0x0005c000)

#define LPC4088_APB1		(0x40080000)
#define LPC4088_SSP0		(LPC4088_APB1 + 0x00008000)
#define LPC4088_UART2		(LPC4088_APB1 + 0x00018000)
#define LPC4088_UART3		(LPC4088_APB1 + 0x0001c000)
#define LPC4088_I2C2		(LPC4088_APB0 + 0x00020000)
#define LPC4088_UART4		(LPC4088_APB1 + 0x00024000)
#define LPC4088_MCI		(LPC4088_APB1 + 0x00040000)
#define LPC4088_SYSCTRL		(LPC4088_APB1 + 0x0007c000)

#define LPC4088_DRAM_CS0	(0xa0000000)

#define LPC4088_PPB		(0xe0000000)
#define LPC4088_SYSTIMER	(LPC4088_PPB + 0x0000e000)
#define LPC4088_SCB		(LPC4088_PPB + 0x0000ed00)
# define LPC4088_SCB_CPUID	(LPC4088_SCB + 0x00)
# define LPC4088_SCB_VTOR	(LPC4088_SCB + 0x08)
#define LPC4088_MPU_TYPE	(LPC4088_PPB + 0x0000ed90)
#define LPC4088_MPU_CTRL	(LPC4088_PPB + 0x0000ed94)
#define LPC4088_MPU_RNR		(LPC4088_PPB + 0x0000ed98)
#define LPC4088_MPU_RBAR	(LPC4088_PPB + 0x0000ed9c)
#define LPC4088_MPU_RASR	(LPC4088_PPB + 0x0000eda0)

struct device_d *lpc4088_add_uart(const unsigned int);

#endif
