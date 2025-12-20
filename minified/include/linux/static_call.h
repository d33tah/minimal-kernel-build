#ifndef _LINUX_STATIC_CALL_H
#define _LINUX_STATIC_CALL_H


#include <linux/types.h>
#include <linux/cpu.h>
#include <linux/static_call_types.h>

#include <asm/static_call.h>

extern void arch_static_call_transform(void *site, void *tramp, void *func, bool tail);



static inline int static_call_init(void) { return 0; }

#define DEFINE_STATIC_CALL(name, _func)					\
	DECLARE_STATIC_CALL(name, _func);				\
	struct static_call_key STATIC_CALL_KEY(name) = {		\
		.func = _func,						\
	};								\
	ARCH_DEFINE_STATIC_CALL_TRAMP(name, _func)

/* DEFINE_STATIC_CALL_NULL, DEFINE_STATIC_CALL_RET0, static_call_cond,
   __static_call_update, static_call_text_reserved removed - never used */

extern long __static_call_return0(void);

/* EXPORT_STATIC_CALL, EXPORT_STATIC_CALL_GPL,
   EXPORT_STATIC_CALL_TRAMP, EXPORT_STATIC_CALL_TRAMP_GPL removed - never used */


#endif  
