#include <linux/fs.h>
#include <linux/sched.h>
#include "internal.h"
#include "mount.h"

/* pin_lock, pin_remove, pin_insert, pin_kill, mnt_pin_kill removed - never called
   (pin_insert was the only function that added to mnt_pins, so mnt_pins is always empty) */
