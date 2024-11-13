// SPDX-License-Identifier: GPL-2.0
/*
 * This contains the io-permission bitmap code - written by obz, with changes
 * by Linus. 32/64 bits code unification by Miguel Botón.
 */
#include <linux/capability.h>
#include <linux/security.h>
#include <linux/syscalls.h>
#include <linux/bitmap.h>
#include <linux/ioport.h>
#include <linux/sched.h>
#include <linux/slab.h>

#include <asm/io_bitmap.h>
#include <asm/desc.h>
#include <asm/syscalls.h>


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
