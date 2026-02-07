/* linux/export.h removed - no EXPORT_SYMBOL used */
/* linux/bvec.h removed - ITER_BVEC never initialized */
#include <linux/uio.h>
#include <linux/pagemap.h>
#include <linux/highmem.h>
/* linux/slab.h removed - no slab functions */
/* linux/vmalloc.h, linux/splice.h removed - not used */
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

/* iterate_bvec macro removed - ITER_BVEC never initialized (~28 LOC) */
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
			} else if (iov_iter_is_kvec(i)) {                      \
				const struct kvec *kvec = i->kvec;             \
				void *base;                                    \
				size_t len;                                    \
				iterate_iovec(i, n, base, len, off, kvec, (K)) \
					i->nr_segs -= kvec - i->kvec;          \
				i->kvec = kvec;                                \
			}                                                      \
			/* ITER_BVEC removed - never initialized */            \
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

/* _copy_from_iter removed - never called */

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
	if (iov_iter_is_kvec(i)) {
		void *kaddr = kmap_local_page(page);
		size_t wanted = _copy_to_iter(kaddr + offset, bytes, i);
		kunmap_local(kaddr);
		return wanted;
	}
	/* ITER_BVEC removed - never initialized */
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

/* iov_iter_zero, copy_page_from_iter_atomic, iov_iter_advance, iov_iter_revert removed - never called */

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
