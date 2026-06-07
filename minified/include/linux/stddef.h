#ifndef _LINUX_STDDEF_H
#define _LINUX_STDDEF_H

#include <linux/compiler_types.h>

#ifndef __always_inline
#define __always_inline inline
#endif

#undef NULL
#define NULL ((void *)0)

enum {
	false	= 0,
	true	= 1
};

#undef offsetof
#define offsetof(TYPE, MEMBER)	__builtin_offsetof(TYPE, MEMBER)

#define sizeof_field(TYPE, MEMBER) sizeof((((TYPE *)0)->MEMBER))

#define offsetofend(TYPE, MEMBER) \
	(offsetof(TYPE, MEMBER)	+ sizeof_field(TYPE, MEMBER))

#endif
