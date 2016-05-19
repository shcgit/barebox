/*
 * Copyright (C) 2014 Alexander Shiyan <shc_work@mail.ru>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 */

#ifndef __MACH_BAREBOX_ARM_HEAD_H
#define __MACH_BAREBOX_ARM_HEAD_H

#include <asm/barebox-arm.h>
#include <asm/unified.h>

static inline void barebox_arm_head(void)
{
	__asm__ __volatile__ (
		/* Stack in SRAM */
		"__PURE_START_:\n"
		".long	0x10007f10\n"
		/*
		 The least-significant bit of each vector must be 1,
		 indicating that the exception handler is Thumb code.
		*/
		".long	2f + 1\n"
		".rept	5\n"
		".long	1f + 1\n"
		".endr\n"
		/* Valid User Code */
		".long	0xefff7fc8\n"
		/* Barebox */
		".asciz	\"barebox\"\n"
		".long	_text\n"
		".long	_barebox_image_size\n"
		"1:	b 1b\n"
		"2:	b barebox_arm_reset_vector\n"
		".org	__PURE_START_ + 0x2fc\n"
		/* Core Read Protection */
		".long	0xdeadbeef"
	);
}

#endif
