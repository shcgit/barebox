/*
 * console_countdown - contdown on the console - interruptible by a keypress
 *
 * Copyright (c) 2007 Sascha Hauer <s.hauer@pengutronix.de>, Pengutronix
 *
 * See file CREDITS for list of people who contributed to this
 * project.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <clock.h>
#include <command.h>
#include <errno.h>
#include <console_countdown.h>
#include <stdio.h>

int console_countdown(int timeout_s, unsigned flags, char *out_key)
{
	uint64_t start, second;
	int countdown, ret = -EINTR;
	int key = 0;

	start = get_time_ns();
	second = start;

	countdown = timeout_s;

	if (!(flags & CONSOLE_COUNTDOWN_SILENT))
		printf("%2d", countdown--);

	do {
		if (tstc()) {
			key = getc();
			if (flags & CONSOLE_COUNTDOWN_ANYKEY)
				goto out;
			if (flags & CONSOLE_COUNTDOWN_RETURN && key == '\n')
				goto out;
			if (flags & CONSOLE_COUNTDOWN_CTRLC && key == 3)
				goto out;
			key = 0;
		}
		if (!(flags & CONSOLE_COUNTDOWN_SILENT) &&
		    is_timeout(second, SECOND)) {
			printf("\b\b%2d", countdown--);
			second += SECOND;
		}
	} while (!is_timeout(start, timeout_s * SECOND));

	ret = 0;

 out:
	if (!(flags & CONSOLE_COUNTDOWN_SILENT))
		printf("\n");
	if (key && out_key)
		*out_key = key;

	return ret;
}
