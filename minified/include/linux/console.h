/*
 *  linux/include/linux/console.h
 *
 *  Copyright (C) 1993        Hamish Macdonald
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 *
 * Changed:
 * 10-Mar-94: Arno Griffioen: Conversion for vt100 emulator port from PC LINUX
 */

#ifndef _LINUX_CONSOLE_H_
#define _LINUX_CONSOLE_H_ 1

struct vc_data;

/**
 * struct consw - callbacks for consoles
 *
 * @con_scroll: move lines from @top to @bottom in direction @dir by @lines.
 *		Return true if no generic handling should be done.
 *		Invoked by csi_M and printing to the console.
 * @con_set_palette: sets the palette of the console to @table (optional)
 * @con_scrolldelta: the contents of the console should be scrolled by @lines.
 *		     Invoked by user. (optional)
 */
struct consw {
	const char *(*con_startup)(void);
	void (*con_init)(struct vc_data *vc);
	void (*con_cursor)(struct vc_data *vc);
};

extern const struct consw *conswitchp;

extern const struct consw vga_con; /* VGA text console */

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
