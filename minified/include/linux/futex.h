#ifndef _LINUX_FUTEX_H
#define _LINUX_FUTEX_H

/* Minimal futex support - only FUTEX_TID_MASK used for MAX_THREADS */
#define FUTEX_TID_MASK		0x3fffffff
/* FUTEX_WAKE, do_futex removed - only caller in mm_release removed */

#endif
