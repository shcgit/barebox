/*
 * (C) Copyright 2012 Jean-Christophe PLAGNIOL-VILLARD <plagnioj@jcrosoft.com>
 *
 * Under GPLv2
 */

#ifndef __SMC911X_PLATFORM_H_
#define __SMC911X_PLATFORM_H_

/**
 * @brief Platform dependent feature:
 * Pass pointer to this structure as part of device_d -> platform_data
 */
struct smc911x_plat {
	u32	shift;
	u32	flags;
	u32	phy_mask;	/* external PHY only: mask out PHYs,
				   e.g. ~(1 << 5) to use PHY addr 5 */
};

#define SMC911X_FORCE_INTERNAL_PHY	0x01
#define SMC911X_FORCE_EXTERNAL_PHY	0x02
#define SMC911X_USE_32BIT		0x04

#endif /* __SMC911X_PLATFORM_H_ */
