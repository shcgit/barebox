#ifndef __ASM_CACHE_H
#define __ASM_CACHE_H

static inline void flush_icache(void)
{
#ifndef CONFIG_CPU_V7M
	asm volatile("mcr p15, 0, %0, c7, c5, 0" : : "r" (0));
#endif
}

int arm_set_cache_functions(void);

void arm_early_mmu_cache_flush(void);
void arm_early_mmu_cache_invalidate(void);

#endif
