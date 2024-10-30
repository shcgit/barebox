/* SPDX-License-Identifier: GPL-2.0-only */
#include <printk.h>
#include <linux/kernel.h>
#include <linux/export.h>
#include <init.h>
#include <stdlib.h>

#ifdef __PBL__
#define STAGE "PBL"
#else
#define STAGE "barebox"
#endif

void __stack_chk_fail(void);

unsigned long __stack_chk_guard = (unsigned long)(0xfeedf00ddeadbeef & ~0UL);

/*
 * Called when gcc's -fstack-protector feature is used, and
 * gcc detects corruption of the on-stack canary value
 */
noinstr void __stack_chk_fail(void)
{
	panic("stack-protector: " STAGE " stack is corrupted in: %pS\n", _RET_IP_);
}
EXPORT_SYMBOL(__stack_chk_fail);

static __no_stack_protector int stackprot_randomize_guard(void)
{
	return get_crypto_bytes(&__stack_chk_guard, sizeof(__stack_chk_guard));
}
late_initcall(stackprot_randomize_guard);
