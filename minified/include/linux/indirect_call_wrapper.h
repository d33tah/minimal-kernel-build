/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_INDIRECT_CALL_WRAPPER_H
#define _LINUX_INDIRECT_CALL_WRAPPER_H

#define INDIRECT_CALL_1(f, f1, ...) f(__VA_ARGS__)
#define INDIRECT_CALL_2(f, f2, f1, ...) f(__VA_ARGS__)
#define INDIRECT_CALL_3(f, f3, f2, f1, ...) f(__VA_ARGS__)
#define INDIRECT_CALL_4(f, f4, f3, f2, f1, ...) f(__VA_ARGS__)
#define INDIRECT_CALLABLE_DECLARE(f)
#define INDIRECT_CALLABLE_SCOPE		static
#define EXPORT_INDIRECT_CALLABLE(f)

/*
 * We can use INDIRECT_CALL_$NR for ipv6 related functions only if ipv6 is
 * builtin, this macro simplify dealing with indirect calls with only ipv4/ipv6
 * alternatives
 */
#if IS_BUILTIN(CONFIG_IPV6)
#define INDIRECT_CALL_INET(f, f2, f1, ...) \
	INDIRECT_CALL_2(f, f2, f1, __VA_ARGS__)
#elif IS_ENABLED(CONFIG_INET)
#define INDIRECT_CALL_INET(f, f2, f1, ...) INDIRECT_CALL_1(f, f1, __VA_ARGS__)
#else
#define INDIRECT_CALL_INET(f, f2, f1, ...) f(__VA_ARGS__)
#endif

#if IS_ENABLED(CONFIG_INET)
#define INDIRECT_CALL_INET_1(f, f1, ...) INDIRECT_CALL_1(f, f1, __VA_ARGS__)
#else
#define INDIRECT_CALL_INET_1(f, f1, ...) f(__VA_ARGS__)
#endif

#endif
