// SPDX-License-Identifier: GPL-2.0
/*
 * This contains the io-permission bitmap code - written by obz, with changes
 * by Linus. 32/64 bits code unification by Miguel Botón.
 */
#include <linux/syscalls.h>
#include <asm/syscalls.h>

#include "asm-generic/errno.h"
#include "asm/ptrace.h"


long ksys_ioperm(unsigned long from, unsigned long num, int turn_on)
{
	return -ENOSYS;
}
SYSCALL_DEFINE3(ioperm, unsigned long, from, unsigned long, num, int, turn_on)
{
	return -ENOSYS;
}

SYSCALL_DEFINE1(iopl, unsigned int, level)
{
	return -ENOSYS;
}
