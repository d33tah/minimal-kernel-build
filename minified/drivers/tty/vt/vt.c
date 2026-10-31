#include <linux/tty.h>
#include <linux/console.h>
#include <linux/vt_kern.h>
#include <linux/selection.h>

#include <linux/device.h>

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
static struct device *tty0dev;

#define VC_DEFAULT_ATTR 0x07

static void vc_scroll_up(struct vc_data *vc)
{
	u16 *top = (u16 *)vc->vc_origin;
	unsigned int moved = (vc->vc_rows - 1) * vc->vc_size_row;

	scr_memmovew(top, top + vc->vc_cols, moved);
	scr_memsetw(top + moved / 2, vc->vc_erase_char, vc->vc_size_row);
}

static void lf(struct vc_data *vc)
{
	if (vc->vc_y + 1 == vc->vc_rows) {
		vc_scroll_up(vc);
	} else {
		vc->vc_y++;
		vc->vc_pos += vc->vc_size_row;
	}
	vc->vc_need_wrap = 0;
}

static void cr(struct vc_data *vc)
{
	vc->vc_pos -= vc->vc_x << 1;
	vc->vc_need_wrap = vc->vc_x = 0;
}

static void bs(struct vc_data *vc)
{
	if (vc->vc_x) {
		vc->vc_pos -= 2;
		vc->vc_x--;
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

	if (vc->vc_x == vc->vc_cols - 1) {
		vc->vc_need_wrap = 1;
	} else {
		vc->vc_x++;
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

struct tty_driver *console_driver;

static struct tty_driver *vt_console_device(struct console *c, int *index)
{
	*index = 0;
	return console_driver;
}

static struct console vt_console_driver = {
	.name = "tty",
	.device = vt_console_device,
	.flags = CON_PRINTBUFFER,
	.index = -1,
};

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
	vc->vc_erase_char = ' ' | (VC_DEFAULT_ATTR << 8);
	if (vc->vc_x >= vc->vc_cols)
		vc->vc_x = vc->vc_cols - 1;
	if (vc->vc_y >= vc->vc_rows)
		vc->vc_y = vc->vc_rows - 1;
	vc->vc_pos =
		vc->vc_origin + vc->vc_y * vc->vc_size_row + (vc->vc_x << 1);
	end = (u16 *)(vc->vc_origin + vc->vc_rows * vc->vc_size_row);
	scr_memsetw((u16 *)vc->vc_pos, vc->vc_erase_char,
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

static struct cdev vc0_cdev;

int __init vty_init(const struct file_operations *console_fops)
{
	cdev_init(&vc0_cdev, console_fops);
	if (cdev_add(&vc0_cdev, MKDEV(TTY_MAJOR, 0), 1) ||
	    register_chrdev_region(MKDEV(TTY_MAJOR, 0), 1, "/dev/vc/0") < 0)
		panic("Couldn't register /dev/tty0 driver\n");
	tty0dev = device_create(tty_class, NULL, MKDEV(TTY_MAJOR, 0), NULL,
				"tty0");
	if (IS_ERR(tty0dev))
		tty0dev = NULL;

	console_driver = tty_alloc_driver(1);
	if (IS_ERR(console_driver))
		panic("Couldn't allocate console driver\n");

	console_driver->name = "tty";
	console_driver->name_base = 1;
	console_driver->major = TTY_MAJOR;
	console_driver->minor_start = 1;
	tty_set_operations(console_driver, &con_ops);
	if (tty_register_driver(console_driver))
		panic("Couldn't register console driver\n");
	return 0;
}
