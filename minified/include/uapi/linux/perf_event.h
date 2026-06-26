/* --- 2025-12-08 10:34 --- Reduced enums to only used values */
#ifndef _UAPI_LINUX_PERF_EVENT_H
#define _UAPI_LINUX_PERF_EVENT_H

#include <linux/types.h>
#include <linux/ioctl.h>
#include <asm/byteorder.h>

/* perf events disabled: struct perf_event_attr (def + fwd decl) and the
 * PERF_TYPE and PERF_COUNT_SW macros were all 0-ref tree-wide; removed. */

#endif  
