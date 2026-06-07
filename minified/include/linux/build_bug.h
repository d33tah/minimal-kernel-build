#ifndef _LINUX_BUILD_BUG_H
#define _LINUX_BUILD_BUG_H

#include <linux/compiler.h>

#define BUILD_BUG_ON_ZERO(e) ((int)(sizeof(struct { int:(-!!(e)); })))

#define BUILD_BUG_ON_INVALID(e) ((void)(sizeof((__force long)(e))))

#define BUILD_BUG_ON_MSG(cond, msg) compiletime_assert(!(cond), msg)

#define BUILD_BUG_ON(condition) \
	BUILD_BUG_ON_MSG(condition, "BUILD_BUG_ON failed: " #condition)

#define static_assert(expr, ...) __static_assert(expr, ##__VA_ARGS__, #expr)
#define __static_assert(expr, msg, ...) _Static_assert(expr, msg)

#endif	 
