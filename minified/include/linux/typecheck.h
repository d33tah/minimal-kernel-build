/* SPDX-License-Identifier: GPL-2.0 */
#ifndef TYPECHECK_H_INCLUDED
#define TYPECHECK_H_INCLUDED

/*
 * Check at compile time that something is of a particular type.
 * Always evaluates to 1 so you may use it easily in comparisons.
 */
#define typecheck(type, x)                     \
	({                                     \
		type __dummy;                  \
		typeof(x) __dummy2;            \
		(void)(&__dummy == &__dummy2); \
		1;                             \
	})

#endif /* TYPECHECK_H_INCLUDED */
