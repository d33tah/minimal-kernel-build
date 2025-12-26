/* Minimal includes for readahead stubs */
#include <linux/syscalls.h>
#include <linux/pagemap.h>
void file_ra_state_init(struct file_ra_state *ra, struct address_space *mapping)
{
}
void page_cache_sync_ra(struct readahead_control *ractl,
			unsigned long req_count)
{
}
SYSCALL_DEFINE3(readahead, int, fd, loff_t, offset, size_t, count)
{
	return -ENOSYS;
}
