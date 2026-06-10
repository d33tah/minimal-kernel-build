#ifndef _LINUX_STATIC_CALL_H
#define _LINUX_STATIC_CALL_H


#include <linux/types.h>
#include <linux/cpu.h>
#include <linux/static_call_types.h>

#include <asm/static_call.h>

static inline int static_call_init(void) { return 0; }

/* DEFINE_STATIC_CALL* / arch_static_call_transform / __static_call_return0 /
   EXPORT_STATIC_CALL* removed - no DEFINE_STATIC_CALL sites exist tree-wide,
   so the out-of-line static_call machinery was never reached. */


#endif  
