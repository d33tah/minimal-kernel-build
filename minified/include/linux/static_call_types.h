#ifndef _STATIC_CALL_TYPES_H
#define _STATIC_CALL_TYPES_H

#include <linux/types.h>
#include <linux/stringify.h>
#include <linux/compiler.h>

#define STATIC_CALL_KEY_PREFIX		__SCK__
/* STATIC_CALL_KEY_PREFIX_STR, STATIC_CALL_KEY_PREFIX_LEN removed - never used */
#define STATIC_CALL_KEY(name)		__PASTE(STATIC_CALL_KEY_PREFIX, name)
/* STATIC_CALL_KEY_STR removed - never used */

#define STATIC_CALL_TRAMP_PREFIX	__SCT__
/* STATIC_CALL_TRAMP_PREFIX_STR, STATIC_CALL_TRAMP_PREFIX_LEN removed - never used */
#define STATIC_CALL_TRAMP(name)		__PASTE(STATIC_CALL_TRAMP_PREFIX, name)
/* STATIC_CALL_TRAMP_STR removed - never used */

/* STATIC_CALL_SITE_TAIL, STATIC_CALL_SITE_INIT, STATIC_CALL_SITE_FLAGS removed - never used */

#define __raw_static_call(name)	(&STATIC_CALL_TRAMP(name))

/* __STATIC_CALL_ADDRESSABLE removed - never used */
#define __static_call(name)	__raw_static_call(name)

struct static_call_key {
	void *func;
};


/* __STATIC_CALL_MOD_ADDRESSABLE, static_call_mod removed - never used */

#define static_call(name)	__static_call(name)


#endif  
