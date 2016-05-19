/*
 * Copyright (C) 2014 Alexander Shiyan <shc_work@mail.ru>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 */

#ifndef __MACH_DEBUG_LL_H__
#define __MACH_DEBUG_LL_H__

#include <io.h>
#include <mach/hardware.h>

#define UART_BASE	LPC4088_UART0
#define UART_THR	0x00
#define UART_DLL	0x00
#define UART_DLM	0x04
#define UART_LCR	0x0c
# define LCR_DLAB	BIT(7)
#define UART_LSR	0x14
# define LSR_THRE	BIT(5)
#define UART_FDR	0x28
#define UART_TER	0x30
# define TER_TXEN	BIT(7)

static inline void lpc408x_uart_lowlevel_init(void)
{
	/* Divisaor latch access */
	writel(LCR_DLAB, UART_BASE + UART_LCR);
	/* Pre-scaler divisor = 5, pre-scaler multiplier = 14 */
	writel(0xe5, UART_BASE + UART_FDR);
	/* 115200 */
	writel(0x18, UART_BASE + UART_DLL);
	writel(0x00, UART_BASE + UART_DLM);
	/* 8N1 */
	writel(0x03, UART_BASE + UART_LCR);
	/* Enable transmitter */
	writel(TER_TXEN, UART_BASE + UART_TER);
}

static inline void PUTC_LL(char c)
{
	/* Wait until there is space in the FIFO */
	do {
	} while (!(readl(UART_BASE + UART_LSR) & LSR_THRE));

	/* Send the character */
	writel(c, UART_BASE + UART_THR);

	/* Wait to make sure it hits the line, in case we die too soon */
	do {
	} while (!(readl(UART_BASE + UART_LSR) & LSR_THRE));
}

#endif
