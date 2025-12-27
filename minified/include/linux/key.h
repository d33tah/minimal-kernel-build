/* --- 2025-12-08 03:55 --- Minimal key.h with reduced includes */
#ifndef _LINUX_KEY_H
#define _LINUX_KEY_H

#include <linux/types.h>

#ifdef __KERNEL__
/* key_serial_t, key_perm_t removed - unused */

struct key;
struct net;

/* All key functions are stubs - CONFIG_KEYS is disabled */
#define key_validate(k)			0
#define key_serial(k)			0
#define key_get(k) 			({ NULL; })
#define key_revoke(k)			do { } while(0)
#define key_invalidate(k)		do { } while(0)
#define key_put(k)			do { } while(0)
#define key_ref_put(k)			do { } while(0)
#define make_key_ref(k, p)		NULL
#define key_ref_to_ptr(k)		NULL
#define is_key_possessed(k)		0
#define key_fsuid_changed(c)		do { } while(0)
#define key_fsgid_changed(c)		do { } while(0)
#define key_init()			do { } while(0)
#define key_free_user_ns(ns)		do { } while(0)
#define key_remove_domain(d)		do { } while(0)

#endif
#endif  
