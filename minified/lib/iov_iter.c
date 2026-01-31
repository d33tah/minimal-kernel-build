#include <linux/export.h>
#include <linux/bvec.h>
#include <linux/uio.h>
#include <linux/pagemap.h>
#include <linux/highmem.h>
#include <linux/slab.h>
/* linux/vmalloc.h removed - vmalloc not used */
#include <linux/splice.h>
/* linux/compat.h removed - compat functions not used */
/* scatterlist.h removed - unused */

/* Inlined from net/checksum.h */
typedef __u16 __sum16;
typedef __u32 __wsum;
#include <linux/instrumented.h>

#define iterate_iovec(i, n, base, len, off, __p, STEP)       \
	{                                                    \
		size_t off = 0;                              \
		size_t skip = i->iov_offset;                 \
		do {                                         \
			len = min(n, __p->iov_len - skip);   \
			if (likely(len)) {                   \
				base = __p->iov_base + skip; \
				len -= (STEP);               \
				off += len;                  \
				skip += len;                 \
				n -= len;                    \
				if (skip < __p->iov_len)     \
					break;               \
			}                                    \
			__p++;                               \
			skip = 0;                            \
		} while (n);                                 \
		i->iov_offset = skip;                        \
		n = off;                                     \
	}

#define iterate_bvec(i, n, base, len, off, p, STEP)                          \
	{                                                                    \
		size_t off = 0;                                              \
		unsigned skip = i->iov_offset;                               \
		while (n) {                                                  \
			unsigned offset = p->bv_offset + skip;               \
			unsigned left;                                       \
			void *kaddr = kmap_local_page(p->bv_page +           \
						      offset / PAGE_SIZE);   \
			base = kaddr + offset % PAGE_SIZE;                   \
			len = min(min(n, (size_t)(p->bv_len - skip)),        \
				  (size_t)(PAGE_SIZE - offset % PAGE_SIZE)); \
			left = (STEP);                                       \
			kunmap_local(kaddr);                                 \
			len -= left;                                         \
			off += len;                                          \
			skip += len;                                         \
			if (skip == p->bv_len) {                             \
				skip = 0;                                    \
				p++;                                         \
			}                                                    \
			n -= len;                                            \
			if (left)                                            \
				break;                                       \
		}                                                            \
		i->iov_offset = skip;                                        \
		n = off;                                                     \
	}

/* iterate_xarray macro removed - ITER_XARRAY never initialized (~40 LOC) */

#define __iterate_and_advance(i, n, base, len, off, I, K)                      \
	{                                                                      \
		if (unlikely(i->count < n))                                    \
			n = i->count;                                          \
		if (likely(n)) {                                               \
			if (likely(iter_is_iovec(i))) {                        \
				const struct iovec *iov = i->iov;              \
				void __user *base;                             \
				size_t len;                                    \
				iterate_iovec(i, n, base, len, off, iov, (I))  \
					i->nr_segs -= iov - i->iov;            \
				i->iov = iov;                                  \
			} else if (iov_iter_is_bvec(i)) {                      \
				const struct bio_vec *bvec = i->bvec;          \
				void *base;                                    \
				size_t len;                                    \
				iterate_bvec(i, n, base, len, off, bvec, (K))  \
					i->nr_segs -= bvec - i->bvec;          \
				i->bvec = bvec;                                \
			} else if (iov_iter_is_kvec(i)) {                      \
				const struct kvec *kvec = i->kvec;             \
				void *base;                                    \
				size_t len;                                    \
				iterate_iovec(i, n, base, len, off, kvec, (K)) \
					i->nr_segs -= kvec - i->kvec;          \
				i->kvec = kvec;                                \
			}                                                      \
			i->count -= n;                                         \
		}                                                              \
	}
#define iterate_and_advance(i, n, base, len, off, I, K) \
	__iterate_and_advance(i, n, base, len, off, I, ((void)(K), 0))

static int copyout(void __user *to, const void *from, size_t n)
{
	if (access_ok(to, n)) {
		instrument_copy_to_user(to, from, n);
		n = raw_copy_to_user(to, from, n);
	}
	return n;
}

static int copyin(void *to, const void __user *from, size_t n)
{
	if (access_ok(from, n)) {
		instrument_copy_from_user(to, from, n);
		n = raw_copy_from_user(to, from, n);
	}
	return n;
}

/* copy_page_to_iter_iovec and copy_page_to_iter_pipe inlined into
 * __copy_page_to_iter below */

/* --- 2025-12-22 04:58 --- Removed PIPE_PARANOIA dead code (~40 LOC) */
/* sanity(i) macro removed - always returned true, callers removed (~1 LOC) */

void iov_iter_init(struct iov_iter *i, unsigned int direction,
		   const struct iovec *iov, unsigned long nr_segs, size_t count)
{
	WARN_ON(direction & ~(READ | WRITE));
	*i = (struct iov_iter){ .iter_type = ITER_IOVEC,
				.nofault = false,
				.data_source = direction,
				.iov = iov,
				.nr_segs = nr_segs,
				.iov_offset = 0,
				.count = count };
}

/* push_pipe, copy_pipe_to_iter removed - iov_iter_pipe never called (~60 LOC) */

size_t _copy_to_iter(const void *addr, size_t bytes, struct iov_iter *i)
{
	/* ITER_PIPE removed - iov_iter_pipe never called in minimal kernel */
	iterate_and_advance(i, bytes, base, len, off,
			    copyout(base, addr + off, len),
			    memcpy(base, addr + off, len))

		return bytes;
}

size_t _copy_from_iter(void *addr, size_t bytes, struct iov_iter *i)
{
	if (unlikely(iov_iter_is_pipe(i))) {
		WARN_ON(1);
		return 0;
	}
	iterate_and_advance(i, bytes, base, len, off,
			    copyin(addr + off, base, len),
			    memcpy(addr + off, base, len))

		return bytes;
}

static inline bool page_copy_sane(struct page *page, size_t offset, size_t n)
{
	struct page *head;
	size_t v = n + offset;

	if (n <= v && v <= PAGE_SIZE)
		return true;

	head = compound_head(page);
	v += (page - head) << PAGE_SHIFT;

	if (likely(n <= v && v <= (page_size(head))))
		return true;
	WARN_ON(1);
	return false;
}

static size_t __copy_page_to_iter(struct page *page, size_t offset,
				  size_t bytes, struct iov_iter *i)
{
	if (likely(iter_is_iovec(i))) {
		/* Inlined copy_page_to_iter_iovec */
		size_t skip, copy, left, wanted;
		const struct iovec *iov;
		char __user *buf;
		void *kaddr, *from;

		if (unlikely(bytes > i->count))
			bytes = i->count;
		if (unlikely(!bytes))
			return 0;

		wanted = bytes;
		iov = i->iov;
		skip = i->iov_offset;
		buf = iov->iov_base + skip;
		copy = min(bytes, iov->iov_len - skip);

		/* kmap inlined */
		might_sleep();
		kaddr = page_address(page);
		from = kaddr + offset;
		left = copyout(buf, from, copy);
		copy -= left;
		skip += copy;
		from += copy;
		bytes -= copy;
		while (unlikely(!left && bytes)) {
			iov++;
			buf = iov->iov_base;
			copy = min(bytes, iov->iov_len);
			left = copyout(buf, from, copy);
			copy -= left;
			skip = copy;
			from += copy;
			bytes -= copy;
		}
		kunmap(page);

		if (skip == iov->iov_len) {
			iov++;
			skip = 0;
		}
		i->count -= wanted - bytes;
		i->nr_segs -= iov - i->iov;
		i->iov = iov;
		i->iov_offset = skip;
		return wanted - bytes;
	}
	if (iov_iter_is_bvec(i) || iov_iter_is_kvec(i)) {
		void *kaddr = kmap_local_page(page);
		size_t wanted = _copy_to_iter(kaddr + offset, bytes, i);
		kunmap_local(kaddr);
		return wanted;
	}
	/* ITER_PIPE, ITER_XARRAY, ITER_DISCARD removed - never initialized */
	WARN_ON(1);
	return 0;
}

size_t copy_page_to_iter(struct page *page, size_t offset, size_t bytes,
			 struct iov_iter *i)
{
	size_t res = 0;
	if (unlikely(!page_copy_sane(page, offset, bytes)))
		return 0;
	page += offset / PAGE_SIZE;
	offset %= PAGE_SIZE;
	while (1) {
		size_t n = __copy_page_to_iter(
			page, offset, min(bytes, (size_t)PAGE_SIZE - offset),
			i);
		res += n;
		bytes -= n;
		if (!bytes || !n)
			break;
		offset += n;
		if (offset == PAGE_SIZE) {
			page++;
			offset = 0;
		}
	}
	return res;
}

/* iov_iter_zero removed - never called */

size_t copy_page_from_iter_atomic(struct page *page, unsigned offset,
				  size_t bytes, struct iov_iter *i)
{
	char *kaddr = kmap_atomic(page), *p = kaddr + offset;
	if (unlikely(!page_copy_sane(page, offset, bytes))) {
		kunmap_atomic(kaddr);
		return 0;
	}
	if (unlikely(iov_iter_is_pipe(i))) {
		kunmap_atomic(kaddr);
		WARN_ON(1);
		return 0;
	}
	/* ITER_DISCARD removed - never initialized */
	iterate_and_advance(i, bytes, base, len, off,
			    copyin(p + off, base, len),
			    memcpy(p + off, base, len)) kunmap_atomic(kaddr);
	return bytes;
}

/* pipe_truncate, pipe_advance removed - ITER_PIPE never used (~30 LOC) */

void iov_iter_advance(struct iov_iter *i, size_t size)
{
	if (unlikely(i->count < size))
		size = i->count;
	if (likely(iter_is_iovec(i) || iov_iter_is_kvec(i))) {
		/* iov_iter_iovec_advance inlined */
		const struct iovec *iov, *end;
		if (i->count) {
			i->count -= size;
			size += i->iov_offset;
			for (iov = i->iov, end = iov + i->nr_segs; iov < end;
			     iov++) {
				if (likely(size < iov->iov_len))
					break;
				size -= iov->iov_len;
			}
			i->iov_offset = size;
			i->nr_segs -= iov - i->iov;
			i->iov = iov;
		}
	} else if (iov_iter_is_bvec(i)) {
		/* bvec_iter_advance inlined */
		struct bvec_iter bi;
		unsigned int idx, bytes;
		bi.bi_size = i->count;
		bi.bi_bvec_done = i->iov_offset;
		bi.bi_idx = 0;
		idx = bi.bi_idx;
		if (WARN_ONCE(size > bi.bi_size,
			      "Attempted to advance past end of bvec iter\n")) {
			bi.bi_size = 0;
		} else {
			bi.bi_size -= size;
			bytes = size + bi.bi_bvec_done;
			while (bytes && bytes >= i->bvec[idx].bv_len) {
				bytes -= i->bvec[idx].bv_len;
				idx++;
			}
			bi.bi_idx = idx;
			bi.bi_bvec_done = bytes;
		}
		i->bvec += bi.bi_idx;
		i->nr_segs -= bi.bi_idx;
		i->count = bi.bi_size;
		i->iov_offset = bi.bi_bvec_done;
	}
	/* ITER_PIPE, ITER_XARRAY, ITER_DISCARD removed - never initialized */
}

void iov_iter_revert(struct iov_iter *i, size_t unroll)
{
	if (!unroll)
		return;
	if (WARN_ON(unroll > MAX_RW_COUNT))
		return;
	i->count += unroll;
	/* ITER_PIPE, ITER_DISCARD removed - never initialized */
	if (unroll <= i->iov_offset) {
		i->iov_offset -= unroll;
		return;
	}
	unroll -= i->iov_offset;
	/* ITER_XARRAY removed - never initialized */
	if (iov_iter_is_bvec(i)) {
		const struct bio_vec *bvec = i->bvec;
		while (1) {
			size_t n = (--bvec)->bv_len;
			i->nr_segs++;
			if (unroll <= n) {
				i->bvec = bvec;
				i->iov_offset = n - unroll;
				return;
			}
			unroll -= n;
		}
	} else {
		const struct iovec *iov = i->iov;
		while (1) {
			size_t n = (--iov)->iov_len;
			i->nr_segs++;
			if (unroll <= n) {
				i->iov = iov;
				i->iov_offset = n - unroll;
				return;
			}
			unroll -= n;
		}
	}
}

void iov_iter_kvec(struct iov_iter *i, unsigned int direction,
		   const struct kvec *kvec, unsigned long nr_segs, size_t count)
{
	WARN_ON(direction & ~(READ | WRITE));
	*i = (struct iov_iter){ .iter_type = ITER_KVEC,
				.data_source = direction,
				.kvec = kvec,
				.nr_segs = nr_segs,
				.iov_offset = 0,
				.count = count };
}

/* iov_iter_pipe, iov_iter_xarray, iov_iter_discard, iov_iter_alignment,
   iov_iter_gap_alignment, iov_iter_get_pages, iov_iter_get_pages_alloc,
   csum_and_copy_*, hash_and_copy_*, iov_iter_npages removed - unused */

/* dup_iter, iovec_from_user, __import_iovec, import_iovec,
   import_single_range, iov_iter_restore removed - unused */
