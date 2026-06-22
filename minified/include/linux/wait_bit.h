#ifndef _LINUX_WAIT_BIT_H
#define _LINUX_WAIT_BIT_H

#include <linux/wait.h>

/* wake_up_bit / __wake_up_bit / bit_waitqueue / wait_bit_init and the
   wait_bit_key/__WAIT_BIT_KEY_INITIALIZER plumbing removed - the sole user
   was a dead __I_NEW wake in fs/inode.c with no waiters. */

/* wait_on_bit_io, wait_on_bit_timeout, wait_on_bit_lock,
   wait_on_bit_lock_io, wait_on_bit_lock_action removed - unused */

/* wait_var_event family removed - unused (init_wait_var_entry, wake_up_var,
   __var_waitqueue, ___wait_var_event, wait_var_event, wait_var_event_killable,
   wait_var_event_timeout, wait_var_event_interruptible removed) */


#endif
