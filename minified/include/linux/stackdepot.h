
#ifndef _LINUX_STACKDEPOT_H
#define _LINUX_STACKDEPOT_H

#include <linux/gfp.h>

typedef u32 depot_stack_handle_t;

depot_stack_handle_t __stack_depot_save(unsigned long *entries,
					unsigned int nr_entries,
					gfp_t gfp_flags, bool can_alloc);

static inline int stack_depot_init(void) { return 0; }

static inline void stack_depot_want_early_init(void) { }

static inline int stack_depot_early_init(void)	{ return 0; }

depot_stack_handle_t stack_depot_save(unsigned long *entries,
				      unsigned int nr_entries, gfp_t gfp_flags);

unsigned int stack_depot_fetch(depot_stack_handle_t handle,
			       unsigned long **entries);

int stack_depot_snprint(depot_stack_handle_t handle, char *buf, size_t size,
		       int spaces);

void stack_depot_print(depot_stack_handle_t stack);

#endif
