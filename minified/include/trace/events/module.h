/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Because linux/module.h has tracepoints in the header, and ftrace.h
 * used to include this file, define_trace.h includes linux/module.h
 * But we do not want the module.h to override the TRACE_SYSTEM macro
 * variable that define_trace.h is processing, so we only set it
 * when module events are being processed, which would happen when
 * CREATE_TRACE_POINTS is defined.
 */
#ifdef CREATE_TRACE_POINTS
#undef TRACE_SYSTEM
#define TRACE_SYSTEM module
#endif

#if !defined(_TRACE_MODULE_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_MODULE_H

#include <linux/tracepoint.h>


#endif /* _TRACE_MODULE_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
