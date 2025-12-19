#ifndef __LINUX_UIO_H
#define __LINUX_UIO_H

#include <linux/kernel.h>
#include <linux/thread_info.h>
#include <linux/mm_types.h>
#include <linux/compiler.h>
#include <linux/types.h>

/* From uapi/linux/uio.h - inlined */
struct iovec
{
	void __user *iov_base;
	__kernel_size_t iov_len;
};

#define UIO_FASTIOV	8
#define UIO_MAXIOV	1024

struct page;
struct pipe_inode_info;

struct kvec {
	void *iov_base;  
	size_t iov_len;
};

enum iter_type {
	 
	ITER_IOVEC,
	ITER_KVEC,
	ITER_BVEC,
	ITER_PIPE,
	ITER_XARRAY,
	ITER_DISCARD,
};

struct iov_iter_state {
	size_t iov_offset;
	size_t count;
	unsigned long nr_segs;
};

struct iov_iter {
	u8 iter_type;
	bool nofault;
	bool data_source;
	size_t iov_offset;
	size_t count;
	union {
		const struct iovec *iov;
		const struct kvec *kvec;
		const struct bio_vec *bvec;
		struct xarray *xarray;
		struct pipe_inode_info *pipe;
	};
	union {
		unsigned long nr_segs;
		struct {
			unsigned int head;
			unsigned int start_head;
		};
		loff_t xarray_start;
	};
};

static inline enum iter_type iov_iter_type(const struct iov_iter *i)
{
	return i->iter_type;
}

static inline void iov_iter_save_state(struct iov_iter *iter,
				       struct iov_iter_state *state)
{
	state->iov_offset = iter->iov_offset;
	state->count = iter->count;
	state->nr_segs = iter->nr_segs;
}

static inline bool iter_is_iovec(const struct iov_iter *i)
{
	return iov_iter_type(i) == ITER_IOVEC;
}

static inline bool iov_iter_is_kvec(const struct iov_iter *i)
{
	return iov_iter_type(i) == ITER_KVEC;
}

static inline bool iov_iter_is_bvec(const struct iov_iter *i)
{
	return iov_iter_type(i) == ITER_BVEC;
}

static inline bool iov_iter_is_pipe(const struct iov_iter *i)
{
	return iov_iter_type(i) == ITER_PIPE;
}

static inline bool iov_iter_is_discard(const struct iov_iter *i)
{
	return iov_iter_type(i) == ITER_DISCARD;
}

static inline bool iov_iter_is_xarray(const struct iov_iter *i)
{
	return iov_iter_type(i) == ITER_XARRAY;
}

size_t copy_page_from_iter_atomic(struct page *page, unsigned offset,
				  size_t bytes, struct iov_iter *i);
void iov_iter_advance(struct iov_iter *i, size_t bytes);
void iov_iter_revert(struct iov_iter *i, size_t bytes);
size_t copy_page_to_iter(struct page *page, size_t offset, size_t bytes,
			 struct iov_iter *i);
size_t copy_page_from_iter(struct page *page, size_t offset, size_t bytes,
			 struct iov_iter *i);

size_t _copy_to_iter(const void *addr, size_t bytes, struct iov_iter *i);
size_t _copy_from_iter(void *addr, size_t bytes, struct iov_iter *i);

static inline size_t copy_folio_to_iter(struct folio *folio, size_t offset,
		size_t bytes, struct iov_iter *i)
{
	return copy_page_to_iter(&folio->page, offset, bytes, i);
}

static __always_inline __must_check
size_t copy_to_iter(const void *addr, size_t bytes, struct iov_iter *i)
{
	if (unlikely(!check_copy_size(addr, bytes, true)))
		return 0;
	else
		return _copy_to_iter(addr, bytes, i);
}

static __always_inline __must_check
size_t copy_from_iter(void *addr, size_t bytes, struct iov_iter *i)
{
	if (unlikely(!check_copy_size(addr, bytes, false)))
		return 0;
	else
		return _copy_from_iter(addr, bytes, i);
}

static __always_inline __must_check
bool copy_from_iter_full(void *addr, size_t bytes, struct iov_iter *i)
{
	size_t copied = copy_from_iter(addr, bytes, i);
	if (likely(copied == bytes))
		return true;
	iov_iter_revert(i, copied);
	return false;
}


#define _copy_mc_to_iter _copy_to_iter

size_t iov_iter_zero(size_t bytes, struct iov_iter *);
unsigned long iov_iter_alignment(const struct iov_iter *i);
unsigned long iov_iter_gap_alignment(const struct iov_iter *i);
void iov_iter_init(struct iov_iter *i, unsigned int direction, const struct iovec *iov,
			unsigned long nr_segs, size_t count);
void iov_iter_kvec(struct iov_iter *i, unsigned int direction, const struct kvec *kvec,
			unsigned long nr_segs, size_t count);
/* iov_iter_bvec, iov_iter_pipe, iov_iter_discard, iov_iter_xarray, iov_iter_get_pages,
   iov_iter_get_pages_alloc, iov_iter_npages, iov_iter_restore, dup_iter
   removed - unused */

static inline size_t iov_iter_count(const struct iov_iter *i)
{
	return i->count;
}

static inline void iov_iter_truncate(struct iov_iter *i, u64 count)
{
	if (i->count > count)
		i->count = count;
}

/* iov_iter_npages_cap, csum_and_copy_*, hash_and_copy_to_iter,
   iovec_from_user, import_iovec, __import_iovec, import_single_range
   removed - unused */

#endif
