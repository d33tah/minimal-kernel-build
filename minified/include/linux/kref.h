
#ifndef _KREF_H_
#define _KREF_H_

#include <linux/refcount.h>

struct kref {
	refcount_t refcount;
};

static inline void kref_init(struct kref *kref)
{
	refcount_set(&kref->refcount, 1);
}


#endif
