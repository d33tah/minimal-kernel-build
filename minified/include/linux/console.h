
#ifndef _LINUX_CONSOLE_H_
#define _LINUX_CONSOLE_H_ 1

#include <linux/types.h>

struct vc_data;
struct module;

struct consw {
	struct module *owner;
	const char *(*con_startup)(void);
	void	(*con_init)(struct vc_data *vc, int init);
	int	(*con_switch)(struct vc_data *vc);
	int	(*con_set_origin)(struct vc_data *vc);
	void	(*con_save_screen)(struct vc_data *vc);
};

extern const struct consw *conswitchp;

extern const struct consw dummy_con;
extern const struct consw vga_con;

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
