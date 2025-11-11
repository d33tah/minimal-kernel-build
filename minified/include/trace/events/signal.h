/* SPDX-License-Identifier: GPL-2.0 */
/* Stub trace events for minimal kernel */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM signal

#if !defined(_TRACE_SIGNAL_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_SIGNAL_H

/* Define required constants for minimal kernel */
#ifndef TRACE_HEADER_MULTI_READ
enum {
	TRACE_SIGNAL_DELIVERED,
	TRACE_SIGNAL_IGNORED,
	TRACE_SIGNAL_ALREADY_PENDING,
	TRACE_SIGNAL_OVERFLOW_FAIL,
	TRACE_SIGNAL_LOSE_INFO,
};
#endif

/* Stub trace events - no actual tracing in minimal kernel */
#define trace_signal_generate(sig, info, task, group, result) do { } while (0)
#define trace_signal_deliver(sig, info, ka) do { } while (0)

#endif /* _TRACE_SIGNAL_H */

/* This part must be outside protection */

