#ifndef _LINUX_TIMEKEEPING_H
#define _LINUX_TIMEKEEPING_H

enum clocksource_ids {
	CSID_GENERIC		= 0,
	CSID_MAX,
};

void timekeeping_init(void);
/* timekeeping_suspended now static in timekeeping.c */

#endif
