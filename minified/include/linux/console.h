
#ifndef _LINUX_CONSOLE_H_
#define _LINUX_CONSOLE_H_ 1

#include <linux/atomic.h>
#include <linux/types.h>

struct vc_data;
struct module;
/* struct console_font, tty_struct, notifier_block forward decls removed - unused */

enum con_scroll {
	SM_UP,
	/* SM_DOWN removed - never used */
};

enum vc_intensity {
	VCI_HALF_BRIGHT,
	VCI_NORMAL,
	VCI_BOLD,
};

struct consw {
	struct module *owner;
	const char *(*con_startup)(void);
	void	(*con_init)(struct vc_data *vc, int init);
	void	(*con_deinit)(struct vc_data *vc);
	/* con_clear removed - never called */
	void	(*con_putc)(struct vc_data *vc, int c, int ypos, int xpos);
	void	(*con_putcs)(struct vc_data *vc, const unsigned short *s,
			int count, int ypos, int xpos);
	void	(*con_cursor)(struct vc_data *vc, int mode);
	bool	(*con_scroll)(struct vc_data *vc, unsigned int top,
			unsigned int bottom, enum con_scroll dir,
			unsigned int lines);
	int	(*con_switch)(struct vc_data *vc);
	/* con_blank, con_font_set, con_font_get, con_font_default, con_resize, con_set_palette, con_scrolldelta removed - never invoked */
	int	(*con_set_origin)(struct vc_data *vc);
	void	(*con_save_screen)(struct vc_data *vc);
	u8	(*con_build_attr)(struct vc_data *vc, u8 color,
			enum vc_intensity intensity,
			bool blink, bool underline, bool reverse, bool italic);
	/* con_invert_region, con_screen_pos, con_getxy removed - never called */
	/* con_flush_scrollback, con_debug_enter, con_debug_leave removed - never called */
};

extern const struct consw *conswitchp;

extern const struct consw dummy_con;
extern const struct consw vga_con;

#define CM_DRAW     (1)
#define CM_ERASE    (2)
/* CM_MOVE removed - never used */


#define CON_PRINTBUFFER	(1)
#define CON_CONSDEV	(2)
#define CON_ENABLED	(4)
#define CON_BOOT	(8)
/* CON_ANYTIME removed - unused */
#define CON_BRL		(32)
/* CON_EXTENDED removed - unused */

struct console {
	char	name[16];
	void	(*write)(struct console *, const char *, unsigned);
	/* read callback removed - never invoked */
	struct tty_driver *(*device)(struct console *, int *);
	void	(*unblank)(void);
	int	(*setup)(struct console *, char *);
	/* exit, match callbacks removed - never invoked */
	short	flags;
	short	index;
	/* cflag, ispeed, ospeed, seq, dropped removed - write-only, never read */
	void	*data;
	struct	 console *next;
};

#define for_each_console(con) \
	for (con = console_drivers; con != NULL; con = con->next)


enum con_flush_mode {
	CONSOLE_FLUSH_PENDING,
	/* CONSOLE_REPLAY_ALL removed - never used */
};

extern void register_console(struct console *);
/* unregister_console made static in printk.c */
extern struct console *console_drivers;
extern void console_lock(void);
extern int console_trylock(void);
extern void console_unlock(void);
extern void console_unblank(void);
extern void console_flush_on_panic(enum con_flush_mode mode);
/* console_device removed - never called */
extern int is_console_locked(void);
/* console_sysfs_notify, vcs_make_sysfs removed - unused */

#define WARN_CONSOLE_UNLOCKED()						\
	WARN_ON(!atomic_read(&ignore_console_lock_warning) &&		\
		!is_console_locked() && !oops_in_progress)
extern atomic_t ignore_console_lock_warning;


extern void console_init(void);


#endif  
