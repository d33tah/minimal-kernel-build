#ifndef _LINUX_STATIC_CALL_H
#define _LINUX_STATIC_CALL_H

#include <linux/types.h>
#include <linux/cpu.h>
#include <linux/static_call_types.h>

static inline int static_call_init(void) { return 0; }

#endif
