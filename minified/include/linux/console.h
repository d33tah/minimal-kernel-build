
#ifndef _LINUX_CONSOLE_H_
#define _LINUX_CONSOLE_H_ 1

#include <linux/atomic.h>

struct vc_data;
struct module;
struct tty_struct;

enum con_scroll {
	SM_UP,
};

struct consw {
	const char *(*con_startup)(void);
	void	(*con_init)(struct vc_data *vc, int init);
	void	(*con_deinit)(struct vc_data *vc);
	void	(*con_putc)(struct vc_data *vc, int c, int ypos, int xpos);
	void	(*con_putcs)(struct vc_data *vc, const unsigned short *s,
			int count, int ypos, int xpos);
	void	(*con_cursor)(struct vc_data *vc, int mode);
	bool	(*con_scroll)(struct vc_data *vc, unsigned int top,
			unsigned int bottom, enum con_scroll dir,
			unsigned int lines);
	int	(*con_switch)(struct vc_data *vc);
	int	(*con_set_origin)(struct vc_data *vc);
	void	(*con_save_screen)(struct vc_data *vc);
	u8	(*con_build_attr)(struct vc_data *vc, u8 color);
};

extern const struct consw *conswitchp;

/* dummy_con removed: the dummy console driver was structurally dead on this
 * build (conswitchp is always &vga_con; no fallback path reaches it). */
extern const struct consw vga_con;

#define CM_DRAW     (1)
#define CM_ERASE    (2)
#define CM_MOVE     (3)


#define CON_PRINTBUFFER	(1)
#define CON_CONSDEV	(2)  
#define CON_ENABLED	(4)
#define CON_BOOT	(8)
#define CON_BRL		(32)

struct console {
	char	name[16];
	struct tty_driver *(*device)(struct console *, int *);
	void	(*unblank)(void);
	short	flags;
	short	index;
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
extern struct tty_driver *console_device(int *);

/*
 * CONFIG_PRINTK is unset, so WARN_ON is a no-op (its argument is evaluated only
 * for an unused value) and the console-lock debug check has no effect.  Folded
 * to nothing, which orphans is_console_locked()/ignore_console_lock_warning and
 * the console_locked tracking they read.
 */
#define WARN_CONSOLE_UNLOCKED()	do { } while (0)


extern void console_init(void);


#endif  
