
#ifndef _LINUX_CONSOLE_H_
#define _LINUX_CONSOLE_H_ 1

#include <linux/types.h>

#define CON_PRINTBUFFER	(1)
#define CON_ENABLED	(4)

struct console {
	char	name[16];
	short	flags;
	short	index;
	struct	 console *next;
};

extern void register_console(struct console *);
extern struct console *console_drivers;
extern void console_lock(void);
extern void console_unlock(void);
extern void console_unblank(void);

extern void console_init(void);

#endif
