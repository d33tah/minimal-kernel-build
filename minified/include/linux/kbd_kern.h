#ifndef _KBD_KERN_H
#define _KBD_KERN_H

#include <linux/tty.h>
#include <linux/interrupt.h>

extern int kbd_init(void);
struct console;
void vt_set_leds_compute_shiftstate(void);

#endif
