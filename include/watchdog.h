/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef INCLUDE_WATCHDOG_H
# define INCLUDE_WATCHDOG_H

#include <poller.h>
#include <driver.h>
#include <param.h>

enum wdog_hw_runnning {
	 WDOG_HW_RUNNING_UNSUPPORTED = PARAM_TRISTATE_UNKNOWN,
	 WDOG_HW_RUNNING = PARAM_TRISTATE_TRUE,
	 WDOG_HW_NOT_RUNNING = PARAM_TRISTATE_FALSE
};

struct watchdog {
	int (*set_timeout)(struct watchdog *, unsigned);
	const char *name;
	struct device_d *hwdev;
	struct device_d dev;
	unsigned int priority;
	unsigned int timeout_max;
	unsigned int poller_timeout_cur;
	unsigned int poller_enable;
	struct poller_async poller;
	struct list_head list;
	int running; /* enum wdog_hw_running */
};

/*
 * Use the following function to check whether or not the hardware watchdog
 * is running
 */
static inline int watchdog_hw_running(struct watchdog *w)
{
	if (w->running == WDOG_HW_RUNNING_UNSUPPORTED)
		return -ENOSYS;

	return w->running == WDOG_HW_RUNNING;
}

#ifdef CONFIG_WATCHDOG
int watchdog_register(struct watchdog *);
int watchdog_deregister(struct watchdog *);
struct watchdog *watchdog_get_default(void);
struct watchdog *watchdog_get_by_name(const char *name);
int watchdog_set_timeout(struct watchdog*, unsigned);
#else
static inline int watchdog_register(struct watchdog *w)
{
	return 0;
}

static inline int watchdog_deregister(struct watchdog *w)
{
	return 0;
}

static inline struct watchdog *watchdog_get_default(void)
{
	return NULL;
}

static inline struct watchdog *watchdog_get_by_name(const char *name)
{
	return NULL;
}

static inline int watchdog_set_timeout(struct watchdog*w, unsigned t)
{
	return 0;
}
#endif

#define WATCHDOG_DEFAULT_PRIORITY 100

#endif /* INCLUDE_WATCHDOG_H */
