// SPDX-License-Identifier: GPL-2.0
/*
 * Handling of different ABIs (personalities).
 *
 * We group personalities into execution domains which have their
 * own handlers for kernel entry points, signal mapping, etc...
 *
 * 2001-05-06	Complete rewrite,  Christoph Hellwig (hch@infradead.org)
 */

#include <linux/personality.h>
#include <linux/sched.h>
#include <linux/syscalls.h>

#include "asm/current.h"
#include "asm/ptrace.h"


SYSCALL_DEFINE1(personality, unsigned int, personality)
{
	unsigned int old = current->personality;

	if (personality != 0xffffffff)
		set_personality(personality);

	return old;
}
