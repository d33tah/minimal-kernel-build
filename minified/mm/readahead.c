/* Minimal includes for readahead stubs */
#include <linux/syscalls.h>
#include <linux/pagemap.h>

void file_ra_state_init(struct file_ra_state *ra, struct address_space *mapping)
{
	 
}

void page_cache_ra_unbounded(struct readahead_control *ractl,
		unsigned long nr_to_read, unsigned long lookahead_size)
{
	 
}

void page_cache_sync_ra(struct readahead_control *ractl,
		unsigned long req_count)
{
	 
}

void page_cache_async_ra(struct readahead_control *ractl,
		struct folio *folio, unsigned long req_count)
{
	 
}

SYSCALL_DEFINE3(readahead, int, fd, loff_t, offset, size_t, count)
{
	return -ENOSYS;   
}

void readahead_expand(struct readahead_control *ractl,
		      loff_t new_start, size_t new_len)
{
	 
}

ssize_t ksys_readahead(int fd, loff_t offset, size_t count)
{
	return -ENOSYS;
}

void page_cache_ra_order(struct readahead_control *ractl,
		struct file_ra_state *ra, unsigned int order)
{
	 
}
