
#ifndef _LINUX_CONSOLE_H_
#define _LINUX_CONSOLE_H_ 1

#include <linux/atomic.h>
#include <linux/types.h>

struct vc_data;
struct console_font_op;
struct console_font;
struct module;
struct tty_struct;
struct notifier_block;

enum con_scroll {
	SM_UP,
	SM_DOWN,
};

enum vc_intensity;

struct consw {
	struct module *owner;
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
	u8	(*con_build_attr)(struct vc_data *vc, u8 color,
			enum vc_intensity intensity,
			bool blink, bool underline, bool reverse, bool italic);
	unsigned long (*con_getxy)(struct vc_data *vc, unsigned long position,
			int *px, int *py);
};

extern const struct consw *conswitchp;

extern const struct consw dummy_con;
extern const struct consw vga_con;

#define CM_DRAW     (1)
#define CM_ERASE    (2)
#define CM_MOVE     (3)


#define CON_PRINTBUFFER	(1)
#define CON_CONSDEV	(2)  
#define CON_ENABLED	(4)
#define CON_BOOT	(8)
#define CON_ANYTIME	(16)  
#define CON_BRL		(32)

struct console {
	char	name[16];
	void	(*write)(struct console *, const char *, unsigned);
	int	(*read)(struct console *, char *, unsigned);
	struct tty_driver *(*device)(struct console *, int *);
	void	(*unblank)(void);
	int	(*setup)(struct console *, char *);
	int	(*exit)(struct console *);
	int	(*match)(struct console *, char *name, int idx, char *options);
	short	flags;
	short	index;
	int	cflag;
	uint	ispeed;
	uint	ospeed;
	u64	seq;
	unsigned long dropped;
	void	*data;
	struct	 console *next;
};

#define for_each_console(con) \
	for (con = console_drivers; con != NULL; con = con->next)


enum con_flush_mode {
	CONSOLE_FLUSH_PENDING,
	CONSOLE_REPLAY_ALL,
};

extern void register_console(struct console *);
extern int unregister_console(struct console *);
extern struct console *console_drivers;
extern void console_lock(void);
extern int console_trylock(void);
extern void console_unlock(void);
extern void console_unblank(void);
extern void console_flush_on_panic(enum con_flush_mode mode);
extern struct tty_driver *console_device(int *);
extern int is_console_locked(void);
extern void console_sysfs_notify(void);

static inline void vcs_make_sysfs(int index) { }

#define WARN_CONSOLE_UNLOCKED()						\
	WARN_ON(!atomic_read(&ignore_console_lock_warning) &&		\
		!is_console_locked() && !oops_in_progress)
extern atomic_t ignore_console_lock_warning;


extern void console_init(void);


#endif  
