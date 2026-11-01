#ifndef __LINUX_UIO_H
#define __LINUX_UIO_H

#include <linux/mm_types.h>

struct iovec {
	void __user *iov_base;
	__kernel_size_t iov_len;
};

struct page;

struct kvec {
	void *iov_base;
	size_t iov_len;
};

enum iter_type {
	ITER_IOVEC,
	ITER_KVEC,
};

struct iov_iter {
	u8 iter_type;
	size_t iov_offset;
	size_t count;
	union {
		const struct iovec *iov;
		const struct kvec *kvec;
	};
};

static inline enum iter_type iov_iter_type(const struct iov_iter *i)
{
	return i->iter_type;
}

static inline bool iter_is_iovec(const struct iov_iter *i)
{
	return iov_iter_type(i) == ITER_IOVEC;
}

static inline bool iov_iter_is_kvec(const struct iov_iter *i)
{
	return iov_iter_type(i) == ITER_KVEC;
}

size_t copy_page_from_iter_atomic(struct page *page, unsigned offset,
				  size_t bytes, struct iov_iter *i);
size_t copy_page_to_iter(struct page *page, size_t offset, size_t bytes,
			 struct iov_iter *i);
size_t _copy_from_iter(void *addr, size_t bytes, struct iov_iter *i);

static inline size_t copy_folio_to_iter(struct folio *folio, size_t offset,
					size_t bytes, struct iov_iter *i)
{
	return copy_page_to_iter(&folio->page, offset, bytes, i);
}

static __always_inline __must_check size_t copy_from_iter(void *addr,
							  size_t bytes,
							  struct iov_iter *i)
{
	if (unlikely(!check_copy_size(addr, bytes, false)))
		return 0;
	else
		return _copy_from_iter(addr, bytes, i);
}

void iov_iter_init(struct iov_iter *i, unsigned int direction,
		   const struct iovec *iov, unsigned long nr_segs,
		   size_t count);
void iov_iter_kvec(struct iov_iter *i, unsigned int direction,
		   const struct kvec *kvec, unsigned long nr_segs,
		   size_t count);

static inline size_t iov_iter_count(const struct iov_iter *i)
{
	return i->count;
}

/*
 * Cap the iov_iter by given limit; note that the second argument is
 * *not* the new size - it's upper limit for such.  Passing it a value
 * greater than the amount of data in iov_iter is fine - it'll just do
 * nothing in that case.
 */
static inline void iov_iter_truncate(struct iov_iter *i, u64 count)
{
	/*
	 * count doesn't have to fit in size_t - comparison extends both
	 * operands to u64 here and any value that would be truncated by
	 * conversion in assignement is by definition greater than all
	 * values of size_t, including old i->count.
	 */
	if (i->count > count)
		i->count = count;
}

#endif
