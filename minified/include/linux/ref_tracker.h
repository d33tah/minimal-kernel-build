// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef _LINUX_REF_TRACKER_H
#define _LINUX_REF_TRACKER_H
#include <linux/refcount.h>
#include <linux/types.h>
#include <linux/spinlock.h>
#include <linux/stackdepot.h>

struct ref_tracker;

struct ref_tracker_dir {
};


static inline void ref_tracker_dir_init(struct ref_tracker_dir *dir,
					unsigned int quarantine_count)
{
}

static inline void ref_tracker_dir_exit(struct ref_tracker_dir *dir)
{
}

static inline void ref_tracker_dir_print(struct ref_tracker_dir *dir,
					 unsigned int display_limit)
{
}

static inline int ref_tracker_alloc(struct ref_tracker_dir *dir,
				    struct ref_tracker **trackerp,
				    gfp_t gfp)
{
	return 0;
}

static inline int ref_tracker_free(struct ref_tracker_dir *dir,
				   struct ref_tracker **trackerp)
{
	return 0;
}


#endif /* _LINUX_REF_TRACKER_H */
