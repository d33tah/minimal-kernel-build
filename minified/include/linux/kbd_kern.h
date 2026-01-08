#ifndef _KBD_KERN_H
#define _KBD_KERN_H

#include <linux/tty.h>
#include <linux/interrupt.h>

extern int kbd_init(void);
/* vt_set_leds_compute_shiftstate removed - empty stub */
#endif
