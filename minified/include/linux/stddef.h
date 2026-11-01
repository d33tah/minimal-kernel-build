/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_STDDEF_H
#define _LINUX_STDDEF_H

#undef NULL
#define NULL ((void *)0)

enum { false = 0, true = 1 };

#undef offsetof
#define offsetof(TYPE, MEMBER) __builtin_offsetof(TYPE, MEMBER)

/**
 * sizeof_field() - Report the size of a struct field in bytes
 *
 * @TYPE: The structure containing the field of interest
 * @MEMBER: The field to return the size of
 */
#define sizeof_field(TYPE, MEMBER) sizeof((((TYPE *)0)->MEMBER))

#endif
