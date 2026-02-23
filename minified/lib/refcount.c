/* Minimal includes for refcount */
#include <linux/refcount.h>
#include <linux/spinlock.h>

void refcount_warn_saturate(refcount_t *r, enum refcount_saturation_type t)
{
}
