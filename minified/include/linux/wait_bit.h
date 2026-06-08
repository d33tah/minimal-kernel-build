#ifndef _LINUX_WAIT_BIT_H
#define _LINUX_WAIT_BIT_H

#include <linux/wait.h>

struct wait_bit_key {
	void			*flags;
	int			bit_nr;
	unsigned long		timeout;
};

#define __WAIT_BIT_KEY_INITIALIZER(word, bit)					\
	{ .flags = word, .bit_nr = bit, }

void __wake_up_bit(struct wait_queue_head *wq_head, void *word, int bit);
void wake_up_bit(void *word, int bit);
struct wait_queue_head *bit_waitqueue(void *word, int bit);
extern void __init wait_bit_init(void);

/* wait_on_bit_io, wait_on_bit_timeout, wait_on_bit_lock,
   wait_on_bit_lock_io, wait_on_bit_lock_action removed - unused */

/* wait_var_event family removed - unused (init_wait_var_entry, wake_up_var,
   __var_waitqueue, ___wait_var_event, wait_var_event, wait_var_event_killable,
   wait_var_event_timeout, wait_var_event_interruptible removed) */


#endif  
