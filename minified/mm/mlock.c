/* Minimal includes for mlock stubs */
#include <linux/syscalls.h>
#include <linux/mm.h>

SYSCALL_DEFINE2(mlock, unsigned long, start, size_t, len)
{
	return -ENOSYS;
}

SYSCALL_DEFINE3(mlock2, unsigned long, start, size_t, len, int, flags)
{
	return -ENOSYS;
}

SYSCALL_DEFINE2(munlock, unsigned long, start, size_t, len)
{
	return -ENOSYS;
}

SYSCALL_DEFINE1(mlockall, int, flags)
{
	return -ENOSYS;
}

SYSCALL_DEFINE0(munlockall)
{
	return -ENOSYS;
}

void mlock_page_drain_local(void)
{
}

void mlock_page_drain_remote(int cpu)
{
}

void mlock_folio(struct folio *folio)
{
}

void munlock_page(struct page *page)
{
}

void mlock_new_page(struct page *page)
{
}

SYSCALL_DEFINE3(readahead, int, fd, loff_t, offset, size_t, count)
{
	return -ENOSYS;
}

SYSCALL_DEFINE3(mprotect, unsigned long, start, size_t, len, unsigned long, prot)
{
	return 0;
}
