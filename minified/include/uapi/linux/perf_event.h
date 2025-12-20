/* --- 2025-12-08 10:34 --- Reduced enums to only used values */
#ifndef _UAPI_LINUX_PERF_EVENT_H
#define _UAPI_LINUX_PERF_EVENT_H

#include <linux/types.h>
#include <linux/ioctl.h>
#include <asm/byteorder.h>

/* Only keep used enum values */
#define PERF_TYPE_BREAKPOINT 5

/* SW event IDs used in mm code */
#define PERF_COUNT_SW_PAGE_FAULTS 2
#define PERF_COUNT_SW_PAGE_FAULTS_MIN 5
#define PERF_COUNT_SW_PAGE_FAULTS_MAJ 6

/* PERF_COUNT_SW_MAX used for array size */
#define PERF_COUNT_SW_MAX 12

/* Minimal struct - only forward declaration used, never instantiated */
struct perf_event_attr {
	__u32 type;
};


#endif  
