
/* All IA32 compat syscalls are stubs, minimal includes needed */
#include <linux/syscalls.h>
#include <linux/compat.h>

SYSCALL_DEFINE3(ia32_truncate64, const char __user *, filename, unsigned long,
		offset_low, unsigned long, offset_high)
{
	return -ENOSYS;
}

/* Stub: ia32 compat syscalls not needed for Hello World */
SYSCALL_DEFINE3(ia32_ftruncate64, unsigned int, fd, unsigned long, offset_low,
		unsigned long, offset_high)
{
	return -ENOSYS;
}
SYSCALL_DEFINE5(ia32_pread64, unsigned int, fd, char __user *, ubuf, u32, count,
		u32, poslo, u32, poshi)
{
	return -ENOSYS;
}
SYSCALL_DEFINE5(ia32_pwrite64, unsigned int, fd, const char __user *, ubuf, u32,
		count, u32, poslo, u32, poshi)
{
	return -ENOSYS;
}
SYSCALL_DEFINE6(ia32_fadvise64_64, int, fd, __u32, offset_low, __u32,
		offset_high, __u32, len_low, __u32, len_high, int, advice)
{
	return -ENOSYS;
}
SYSCALL_DEFINE4(ia32_readahead, int, fd, unsigned int, off_lo, unsigned int,
		off_hi, size_t, count)
{
	return -ENOSYS;
}
SYSCALL_DEFINE6(ia32_sync_file_range, int, fd, unsigned int, off_low,
		unsigned int, off_hi, unsigned int, n_low, unsigned int, n_hi,
		int, flags)
{
	return -ENOSYS;
}
SYSCALL_DEFINE5(ia32_fadvise64, int, fd, unsigned int, offset_lo, unsigned int,
		offset_hi, size_t, len, int, advice)
{
	return -ENOSYS;
}
SYSCALL_DEFINE6(ia32_fallocate, int, fd, int, mode, unsigned int, offset_lo,
		unsigned int, offset_hi, unsigned int, len_lo, unsigned int,
		len_hi)
{
	return -ENOSYS;
}
