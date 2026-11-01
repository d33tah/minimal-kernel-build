// SPDX-License-Identifier: GPL-2.0
/*
 * Callers outside of misc.c need access to the error reporting routines,
 * but the *_putstr() functions need to stay in misc.c because of how
 * memcpy() and memmove() are defined for the compressed boot environment.
 */
#include "misc.h"

void warn(char *m)
{
	__putstr("\n\n");
	__putstr(m);
	__putstr("\n\n");
}

void error(char *m)
{
	warn(m);
	__putstr(" -- System halted");

	while (1)
		asm("hlt");
}
