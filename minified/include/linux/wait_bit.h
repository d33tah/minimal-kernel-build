#ifndef _LINUX_WAIT_BIT_H
#define _LINUX_WAIT_BIT_H

#include <linux/wait.h>

struct wait_bit_key {
	void			*flags;
	int			bit_nr;
	unsigned long		timeout;
};

struct wait_bit_queue_entry {
	struct wait_bit_key	key;
	struct wait_queue_entry	wq_entry;
};

#define __WAIT_BIT_KEY_INITIALIZER(word, bit)					\
	{ .flags = word, .bit_nr = bit, }

typedef int wait_bit_action_f(struct wait_bit_key *key, int mode);

void __wake_up_bit(struct wait_queue_head *wq_head, void *word, int bit);
int __wait_on_bit(struct wait_queue_head *wq_head, struct wait_bit_queue_entry *wbq_entry, wait_bit_action_f *action, unsigned int mode);
void wake_up_bit(void *word, int bit);
int out_of_line_wait_on_bit(void *word, int, wait_bit_action_f *action, unsigned int mode);
/* __wait_on_bit_lock, out_of_line_wait_on_bit_lock removed - unused */
struct wait_queue_head *bit_waitqueue(void *word, int bit);
extern void __init wait_bit_init(void);

int wake_bit_function(struct wait_queue_entry *wq_entry, unsigned mode, int sync, void *key);

#define DEFINE_WAIT_BIT(name, word, bit)					\
	struct wait_bit_queue_entry name = {					\
		.key = __WAIT_BIT_KEY_INITIALIZER(word, bit),			\
		.wq_entry = {							\
			.private	= current,				\
			.func		= wake_bit_function,			\
			.entry		=					\
				LIST_HEAD_INIT((name).wq_entry.entry),		\
		},								\
	}

extern int bit_wait(struct wait_bit_key *key, int mode);

static inline int
wait_on_bit(unsigned long *word, int bit, unsigned mode)
{
	might_sleep();
	if (!test_bit(bit, word))
		return 0;
	return out_of_line_wait_on_bit(word, bit, bit_wait, mode);
}

/* wait_on_bit_io, wait_on_bit_timeout, wait_on_bit_lock,
   wait_on_bit_lock_io, wait_on_bit_lock_action removed - unused */

/* wait_var_event family removed - unused (init_wait_var_entry, wake_up_var,
   __var_waitqueue, ___wait_var_event, wait_var_event, wait_var_event_killable,
   wait_var_event_timeout, wait_var_event_interruptible removed) */

/* clear_and_wake_up_bit removed - unused */

#endif  
