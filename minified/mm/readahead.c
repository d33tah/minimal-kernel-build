/* Stub readahead - returns ENOSYS for minimal kernel */
#include <linux/syscalls.h>
#include <linux/pagemap.h>
/* file_ra_state_init call removed - function no longer needed */
void page_cache_sync_ra(struct readahead_control *ractl,
			unsigned long req_count)
{
}
SYSCALL_DEFINE3(readahead, int, fd, loff_t, offset, size_t, count)
{
	return -ENOSYS;
}
