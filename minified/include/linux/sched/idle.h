#ifndef _LINUX_SCHED_IDLE_H
#define _LINUX_SCHED_IDLE_H

#include <linux/sched.h>

static inline void __current_set_polling(void) {
	/* TIF_POLLING_NRFLAG never tested in this build -> no-op */
}

static inline void __current_clr_polling(void) {
	/* TIF_POLLING_NRFLAG never tested in this build -> no-op */
}

static inline bool __must_check current_clr_polling_and_test(void) {
	__current_clr_polling();

	 
	smp_mb__after_atomic();

	return unlikely(tif_need_resched()); }


#endif
