#ifndef _LINUX_TTY_FLAGS_H
#define _LINUX_TTY_FLAGS_H

/* Reduced tty_flags.h - only keep used flags */

#define ASYNCB_HUP_NOTIFY	 0
#define ASYNC_HUP_NOTIFY	(1U << ASYNCB_HUP_NOTIFY)

/* All other ASYNCB_ and ASYNC_ flags removed - unused */

#endif
