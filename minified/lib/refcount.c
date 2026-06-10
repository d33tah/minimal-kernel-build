/* Minimal includes for refcount */
#include <linux/refcount.h>
#include <linux/spinlock.h>

#define REFCOUNT_WARN(str)	WARN_ONCE(1, "refcount_t: " str ".\n")

void refcount_warn_saturate(refcount_t *r, enum refcount_saturation_type t)
{

}
