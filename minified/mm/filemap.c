
#include <linux/swap.h>
#include <linux/uio.h>
#include <linux/backing-dev.h>
#include "internal.h"

/*
 * The decompressed kernel proper never runs (the decompressor stub halts after
 * printing "Hello, World!"), so the page-cache read path is dead at runtime.
 * Only these externally-referenced signatures need to survive the link.
 */

void __init pagecache_init(void)
{
}

void folio_unlock(struct folio *folio)
{
}

ssize_t generic_file_read_iter(struct kiocb *iocb, struct iov_iter *iter)
{
	return -ENOSYS;
}
