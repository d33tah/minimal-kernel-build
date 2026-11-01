// SPDX-License-Identifier: GPL-2.0
/*
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

/*
 * Hopefully this will be a rather complete VT102 implementation.
 *
 * Beeping thanks to John T Kohl.
 *
 * Virtual Consoles, Screen Blanking, Screen Dumping, Color, Graphics
 *   Chars, and VT100 enhancements by Peter MacDonald.
 *
 * Copy and paste function by Andrew Haylett,
 *   some enhancements by Alessandro Rubini.
 *
 * Code to check for different video-cards mostly by Galen Hunt,
 * <g-hunt@ee.utah.edu>
 *
 * Rudimentary ISO 10646/Unicode/UTF-8 character set support by
 * Markus Kuhn, <mskuhn@immd4.informatik.uni-erlangen.de>.
 *
 * Dynamic allocation of consoles, aeb@cwi.nl, May 1994
 * Resizing of consoles, aeb, 940926
 *
 * Code for xterm like mouse click reporting by Peter Orbaek 20-Jul-94
 * <poe@daimi.aau.dk>
 *
 * User-defined bell sound, new setterm control sequences and printk
 * redirection by Martin Mares <mj@k332.feld.cvut.cz> 19-Nov-95
 *
 * APM screenblank bug fixed Takashi Manabe <manabe@roy.dsl.tutics.tut.jp>
 *
 * Merge with the abstract console driver by Geert Uytterhoeven
 * <geert@linux-m68k.org>, Jan 1997.
 *
 *   Original m68k console driver modifications by
 *
 *     - Arno Griffioen <arno@usn.nl>
 *     - David Carter <carter@cs.bris.ac.uk>
 * 
 *   The abstract console driver provides a generic interface for a text
 *   console. It supports VGA text mode, frame buffer based graphical consoles
 *   and special graphics processors that are only accessible through some
 *   registers (e.g. a TMS340x0 GSP).
 *
 *   The interface to the hardware is specified using a special structure
 *   (struct consw) which contains function pointers to console operations
 *   (see <linux/console.h> for more information).
 *
 * Support for changeable cursor shape
 * by Pavel Machek <pavel@atrey.karlin.mff.cuni.cz>, August 1997
 *
 * Ported to i386 and con_scrolldelta fixed
 * by Emmanuel Marty <core@ggi-project.org>, April 1998
 *
 * Resurrected character buffers in videoram plus lots of other trickery
 * by Martin Mares <mj@atrey.karlin.mff.cuni.cz>, July 1998
 *
 * Removed old-style timers, introduced console_timer, made timer
 * deletion SMP-safe.  17Jun00, Andrew Morton
 *
 * Removed console_lock, enabled interrupts across all console operations
 * 13 March 2001, Andrew Morton
 *
 * Fixed UTF-8 mode so alternate charset modes always work according
 * to control sequences interpreted in do_con_trol function
 * preserving backward VT100 semigraphics compatibility,
 * malformed UTF sequences represented as sequences of replacement glyphs,
 * original codes or '?' as a last resort if replacement glyph is undefined
 * by Adam Tla/lka <atlka@pg.gda.pl>, Aug 2006
 */

#include <linux/tty.h>
#include <linux/console.h>
#include <linux/vt_kern.h>
#include <linux/selection.h>

/*
 * One virtual console on the text buffer of the console driver (vgacon).
 *
 * Bytes written to it are interpreted as follows; there is no escape
 * sequence parser, no UTF-8 decoding and no character set translation:
 *   '\n' (LF)  carriage return + line feed (the console has no line
 *              discipline, so the ONLCR mapping of a default tty is done here)
 *   '\r' (CR)  carriage return
 *   '\b' (BS)  cursor one column left, unless already in column 0
 *   other bytes below 0x20, and 0x7f: ignored
 *   every other byte: stored in the cell under the cursor with the default
 *              attribute (light grey on black); the hardware font picks the glyph
 * Writing past the last column wraps to the next line; a line feed on the
 * last line scrolls the buffer up by one line (memmove) and clears the new
 * bottom line.
 */

const struct consw *conswitchp;

static struct vc_data vc0;

#define VC_DEFAULT_ATTR 0x07

static void con_scroll(struct vc_data *vc)
{
	u16 *top = (u16 *)vc->vc_origin;
	unsigned int moved = (vc->vc_rows - 1) * vc->vc_size_row;

	scr_memmovew(top, top + vc->vc_cols, moved);
	scr_memsetw(top + moved / 2, vc->vc_video_erase_char, vc->vc_size_row);
}

static void lf(struct vc_data *vc)
{
	if (vc->state.y + 1 == vc->vc_rows) {
		con_scroll(vc);
	} else {
		vc->state.y++;
		vc->vc_pos += vc->vc_size_row;
	}
	vc->vc_need_wrap = 0;
}

static void cr(struct vc_data *vc)
{
	vc->vc_pos -= vc->state.x << 1;
	vc->vc_need_wrap = vc->state.x = 0;
}

static void bs(struct vc_data *vc)
{
	if (vc->state.x) {
		vc->vc_pos -= 2;
		vc->state.x--;
		vc->vc_need_wrap = 0;
	}
}

static void vc_putc(struct vc_data *vc, unsigned char c)
{
	if (vc->vc_need_wrap) {
		cr(vc);
		lf(vc);
	}

	scr_writew(c | (VC_DEFAULT_ATTR << 8), (u16 *)vc->vc_pos);

	if (vc->state.x == vc->vc_cols - 1) {
		vc->vc_need_wrap = 1;
	} else {
		vc->state.x++;
		vc->vc_pos += 2;
	}
}

static int con_write(struct tty_struct *tty, const unsigned char *buf,
		     int count)
{
	struct vc_data *vc = tty->driver_data;
	int n;

	console_lock();
	for (n = 0; n < count; n++) {
		unsigned char c = buf[n];

		if (c == '\n') {
			cr(vc);
			lf(vc);
		} else if (c == '\r') {
			cr(vc);
		} else if (c == '\b') {
			bs(vc);
		} else if (c >= 0x20 && c != 0x7f) {
			vc_putc(vc, c);
		}
	}
	vc->vc_sw->con_cursor(vc);
	console_unlock();
	return count;
}

static struct tty_driver *console_driver;

static struct tty_driver *vt_console_device(struct console *c, int *index)
{
	*index = 0;
	return console_driver;
}

static struct console vt_console_driver = {
	.device = vt_console_device,
};

/*
 * Allocate the console screen memory.
 */
static int con_install(struct tty_driver *driver, struct tty_struct *tty)
{
	if (!vc0.vc_sw)
		return -ENODEV;
	tty->driver_data = &vc0;
	return tty_standard_install(driver, tty);
}

/*
 * Take over the text screen the boot loader left: keep what is above the
 * cursor, clear from the cursor to the end of the screen.
 */
static int __init con_init(void)
{
	struct vc_data *vc = &vc0;
	u16 *end;

	if (!conswitchp->con_startup())
		return 0;

	console_lock();
	conswitchp->con_init(vc);
	vc->vc_sw = conswitchp;
	vc->vc_size_row = vc->vc_cols << 1;
	vc->vc_video_erase_char = ' ' | (VC_DEFAULT_ATTR << 8);
	if (vc->state.x >= vc->vc_cols)
		vc->state.x = vc->vc_cols - 1;
	if (vc->state.y >= vc->vc_rows)
		vc->state.y = vc->vc_rows - 1;
	vc->vc_pos = vc->vc_origin + vc->state.y * vc->vc_size_row +
		     (vc->state.x << 1);
	end = (u16 *)(vc->vc_origin + vc->vc_rows * vc->vc_size_row);
	scr_memsetw((u16 *)vc->vc_pos, vc->vc_video_erase_char,
		    (unsigned long)end - vc->vc_pos);
	vc->vc_sw->con_cursor(vc);
	console_unlock();

	register_console(&vt_console_driver);
	return 0;
}
console_initcall(con_init);

static const struct tty_operations con_ops = {
	.install = con_install,
	.write = con_write,
};

int __init vty_init(void)
{
	console_driver = tty_alloc_driver(1);
	if (IS_ERR(console_driver))
		panic("Couldn't allocate console driver\n");
	tty_set_operations(console_driver, &con_ops);
	return 0;
}
