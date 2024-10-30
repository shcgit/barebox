/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef DECOMPRESS_UNLZ4_H
#define DECOMPRESS_UNLZ4_H

int decompress_unlz4(unsigned char *inbuf, long len,
	long(*fill)(void*, unsigned long),
	long(*flush)(void*, unsigned long),
	unsigned char *output,
	long *pos,
	void(*error)(char *x));
#endif
