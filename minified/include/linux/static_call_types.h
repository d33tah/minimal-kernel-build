#ifndef _STATIC_CALL_TYPES_H
#define _STATIC_CALL_TYPES_H

#include <linux/types.h>
#include <linux/stringify.h>
#include <linux/compiler.h>

#define STATIC_CALL_TRAMP_PREFIX	__SCT__
#define STATIC_CALL_TRAMP(name)		__PASTE(STATIC_CALL_TRAMP_PREFIX, name)

#define __raw_static_call(name)	(&STATIC_CALL_TRAMP(name))

#define __static_call(name)	__raw_static_call(name)


#define static_call(name)	__static_call(name)

#endif  
