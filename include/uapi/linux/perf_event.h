/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Minimal stub - Performance events disabled for Hello World kernel
 */
#ifndef _UAPI_LINUX_PERF_EVENT_H
#define _UAPI_LINUX_PERF_EVENT_H

#include <linux/types.h>
#include <linux/ioctl.h>
#include <asm/byteorder.h>

/* Minimal enum stubs */
enum perf_type_id {
	PERF_TYPE_HARDWARE = 0,
	PERF_TYPE_SOFTWARE = 1,
	PERF_TYPE_MAX,
};

enum perf_hw_id {
	PERF_COUNT_HW_MAX,
};

enum perf_sw_ids {
	PERF_COUNT_SW_MAX,
};

enum perf_event_sample_format {
	PERF_SAMPLE_MAX = 1U << 25,
};

enum perf_event_read_format {
	PERF_FORMAT_MAX = 1U << 5,
};

/* Minimal attribute structure - required by some headers */
struct perf_event_attr {
	__u32	type;
	__u32	size;
	__u64	config;
	union {
		__u64	sample_period;
		__u64	sample_freq;
	};
	__u64	sample_type;
	__u64	read_format;
};

/* Minimal mmap page structure */
struct perf_event_mmap_page {
	__u32	version;
	__u32	compat_version;
	__u32	lock;
	__u32	index;
	__s64	offset;
	__u64	time_enabled;
	__u64	time_running;
	__u64	capabilities;
};

struct perf_event_header {
	__u32	type;
	__u16	misc;
	__u16	size;
};

enum perf_event_type {
	PERF_RECORD_MMAP = 1,
	PERF_RECORD_MAX,
};

/* IOCTLs */
#define PERF_EVENT_IOC_ENABLE		_IO ('$', 0)
#define PERF_EVENT_IOC_DISABLE		_IO ('$', 1)
#define PERF_EVENT_IOC_REFRESH		_IO ('$', 2)
#define PERF_EVENT_IOC_RESET		_IO ('$', 3)
#define PERF_EVENT_IOC_PERIOD		_IOW('$', 4, __u64)
#define PERF_EVENT_IOC_SET_OUTPUT	_IO ('$', 5)
#define PERF_EVENT_IOC_SET_FILTER	_IOW('$', 6, char *)
#define PERF_EVENT_IOC_ID		_IOR('$', 7, __u64 *)

#endif /* _UAPI_LINUX_PERF_EVENT_H */
