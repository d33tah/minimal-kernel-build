// SPDX-License-Identifier: GPL-2.0
// Stubbed mlock.c - minimal implementation for Hello World kernel

#include <linux/capability.h>
#include <linux/mman.h>
#include <linux/mm.h>
#include <linux/sched/user.h>
#include <linux/syscalls.h>
#include <linux/export.h>
#include <linux/hugetlb.h>

bool can_do_mlock(void)
{
	return false;  /* No mlock allowed */
}

SYSCALL_DEFINE2(mlock, unsigned long, start, size_t, len)
{
	return -ENOSYS;  /* Not implemented */
}

SYSCALL_DEFINE3(mlock2, unsigned long, start, size_t, len, int, flags)
{
	return -ENOSYS;  /* Not implemented */
}

SYSCALL_DEFINE2(munlock, unsigned long, start, size_t, len)
{
	return -ENOSYS;  /* Not implemented */
}

SYSCALL_DEFINE1(mlockall, int, flags)
{
	return -ENOSYS;  /* Not implemented */
}

SYSCALL_DEFINE0(munlockall)
{
	return -ENOSYS;  /* Not implemented */
}

/* Additional required functions - no-op stubs */
void mlock_page_drain_local(void)
{
	/* No-op */
}

void mlock_page_drain_remote(int cpu)
{
	/* No-op */
}

void mlock_folio(struct folio *folio)
{
	/* No-op */
}

void munlock_page(struct page *page)
{
	/* No-op */
}

void mlock_new_page(struct page *page)
{
	/* No-op */
}
