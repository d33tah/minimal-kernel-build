#ifndef _LINUX_CONSOLE_H_
#define _LINUX_CONSOLE_H_ 1

struct vc_data;

struct consw {
	const char *(*con_startup)(void);
	void (*con_init)(struct vc_data *vc);
	void (*con_cursor)(struct vc_data *vc);
};

extern const struct consw *conswitchp;

extern const struct consw vga_con;

/* A console: ->device names the tty driver line behind /dev/console. */
struct console {
	struct tty_driver *(*device)(struct console *, int *);
	struct console *next;
};

extern void register_console(struct console *);
extern void console_lock(void);
extern void console_unlock(void);
extern struct tty_driver *console_device(int *);

extern void console_init(void);

#endif
