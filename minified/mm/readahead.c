// SPDX-License-Identifier: GPL-2.0-only
// Stubbed readahead.c - minimal implementation for Hello World kernel

#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/pagemap.h>
#include <linux/syscalls.h>
#include <linux/export.h>

/* Stub: no-op readahead implementation */
void file_ra_state_init(struct file_ra_state *ra, struct address_space *mapping)
{
	/* No-op */
}

void page_cache_ra_unbounded(struct readahead_control *ractl,
		unsigned long nr_to_read, unsigned long lookahead_size)
{
	/* No-op */
}

void page_cache_sync_ra(struct readahead_control *ractl,
		unsigned long req_count)
{
	/* No-op */
}

void page_cache_async_ra(struct readahead_control *ractl,
		struct folio *folio, unsigned long req_count)
{
	/* No-op */
}

SYSCALL_DEFINE3(readahead, int, fd, loff_t, offset, size_t, count)
{
	return -ENOSYS;  /* Not implemented */
}

void readahead_expand(struct readahead_control *ractl,
		      loff_t new_start, size_t new_len)
{
	/* No-op */
}

/* Additional missing functions */
ssize_t ksys_readahead(int fd, loff_t offset, size_t count)
{
	return -ENOSYS;
}

void page_cache_ra_order(struct readahead_control *ractl,
		struct file_ra_state *ra, unsigned int order)
{
	/* No-op */
}
