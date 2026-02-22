
#ifndef _LINUX_CONSOLE_H_
#define _LINUX_CONSOLE_H_ 1

#include <linux/atomic.h>
#include <linux/types.h>

struct vc_data;
struct module;

enum con_scroll {
	SM_UP,
};

struct consw {
	struct module *owner;
	const char *(*con_startup)(void);
	void	(*con_init)(struct vc_data *vc, int init);
	bool	(*con_scroll)(struct vc_data *vc, unsigned int top,
			unsigned int bottom, enum con_scroll dir,
			unsigned int lines);
	int	(*con_switch)(struct vc_data *vc);
	int	(*con_set_origin)(struct vc_data *vc);
	void	(*con_save_screen)(struct vc_data *vc);
};

extern const struct consw *conswitchp;

extern const struct consw dummy_con;
extern const struct consw vga_con;

#define CON_PRINTBUFFER	(1)
#define CON_CONSDEV	(2)
#define CON_ENABLED	(4)
#define CON_BOOT	(8)
#define CON_BRL		(32)

struct console {
	char	name[16];
	void	(*write)(struct console *, const char *, unsigned);
	struct tty_driver *(*device)(struct console *, int *);
	void	(*unblank)(void);
	int	(*setup)(struct console *, char *);
	short	flags;
	short	index;
	void	*data;
	struct	 console *next;
};

#define for_each_console(con) \
	for (con = console_drivers; con != NULL; con = con->next)

enum con_flush_mode {
	CONSOLE_FLUSH_PENDING,
};

extern void register_console(struct console *);
extern struct console *console_drivers;
extern void console_lock(void);
extern void console_unlock(void);
extern void console_unblank(void);
extern void console_flush_on_panic(enum con_flush_mode mode);
extern int is_console_locked(void);

#define WARN_CONSOLE_UNLOCKED()						\
	WARN_ON(!atomic_read(&ignore_console_lock_warning) &&		\
		!is_console_locked() && !oops_in_progress)
extern atomic_t ignore_console_lock_warning;

extern void console_init(void);

#endif
