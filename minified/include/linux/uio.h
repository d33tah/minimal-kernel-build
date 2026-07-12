#ifndef __LINUX_UIO_H
#define __LINUX_UIO_H

#include <linux/kernel.h>
#include <linux/thread_info.h>
#include <linux/mm_types.h>

struct iovec {
	void __user *iov_base;
	__kernel_size_t iov_len; };

struct page;
struct pipe_inode_info;
struct bio_vec;

struct kvec { void *iov_base; size_t iov_len; };

enum iter_type { ITER_IOVEC, ITER_KVEC, };

struct iov_iter { u8 iter_type; size_t iov_offset, count; union { const struct iovec *iov; const struct kvec *kvec; const struct bio_vec *bvec; struct xarray *xarray; struct pipe_inode_info *pipe; }; union { unsigned long nr_segs; struct { unsigned int head, start_head; }; loff_t xarray_start; }; };

static inline enum iter_type iov_iter_type(const struct iov_iter *i) {
	return i->iter_type; }


static inline bool iter_is_iovec(const struct iov_iter *i) {
	return iov_iter_type(i) == ITER_IOVEC; }

static inline bool iov_iter_is_kvec(const struct iov_iter *i) {
	return iov_iter_type(i) == ITER_KVEC; }


size_t copy_page_from_iter_atomic(struct page *page, unsigned offset, size_t bytes, struct iov_iter *i);
void iov_iter_revert(struct iov_iter *i, size_t bytes);
size_t copy_page_to_iter(struct page *page, size_t offset, size_t bytes, struct iov_iter *i);

size_t _copy_to_iter(const void *addr, size_t bytes, struct iov_iter *i);
size_t _copy_from_iter(void *addr, size_t bytes, struct iov_iter *i);

static inline size_t copy_folio_to_iter(struct folio *folio, size_t offset, size_t bytes, struct iov_iter *i) {
	return copy_page_to_iter(&folio->page, offset, bytes, i); }

static __always_inline __must_check
size_t copy_from_iter(void *addr, size_t bytes, struct iov_iter *i) {
	if (unlikely(!check_copy_size(addr, bytes, false)))
		return 0;
	else
		return _copy_from_iter(addr, bytes, i); }


void iov_iter_init(struct iov_iter *i, unsigned int direction, const struct iovec *iov, unsigned long nr_segs, size_t count);
void iov_iter_kvec(struct iov_iter *i, unsigned int direction, const struct kvec *kvec, unsigned long nr_segs, size_t count);
/* iov_iter_bvec, iov_iter_pipe, iov_iter_discard, iov_iter_xarray, iov_iter_get_pages,
   iov_iter_get_pages_alloc, iov_iter_npages, iov_iter_restore, dup_iter
   removed - unused */

static inline size_t iov_iter_count(const struct iov_iter *i) {
	return i->count; }

static inline void iov_iter_truncate(struct iov_iter *i, u64 count) {
	if (i->count > count)
		i->count = count; }

/* iov_iter_npages_cap, csum_and_copy_*, hash_and_copy_to_iter,
   iovec_from_user, import_iovec, __import_iovec, import_single_range
   removed - unused */

#endif
