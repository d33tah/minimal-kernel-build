#include <linux/export.h>
#include <linux/bvec.h>
#include <linux/uio.h>
#include <linux/pagemap.h>
#include <linux/highmem.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/splice.h>
#include <linux/compat.h>
#include <linux/scatterlist.h>

/* Inlined from net/checksum.h */
typedef __u16 __sum16;
typedef __u32 __wsum;
#include <linux/instrumented.h>

#define PIPE_PARANOIA  

#define iterate_iovec(i, n, base, len, off, __p, STEP) {	\
	size_t off = 0;						\
	size_t skip = i->iov_offset;				\
	do {							\
		len = min(n, __p->iov_len - skip);		\
		if (likely(len)) {				\
			base = __p->iov_base + skip;		\
			len -= (STEP);				\
			off += len;				\
			skip += len;				\
			n -= len;				\
			if (skip < __p->iov_len)		\
				break;				\
		}						\
		__p++;						\
		skip = 0;					\
	} while (n);						\
	i->iov_offset = skip;					\
	n = off;						\
}

#define iterate_bvec(i, n, base, len, off, p, STEP) {		\
	size_t off = 0;						\
	unsigned skip = i->iov_offset;				\
	while (n) {						\
		unsigned offset = p->bv_offset + skip;		\
		unsigned left;					\
		void *kaddr = kmap_local_page(p->bv_page +	\
					offset / PAGE_SIZE);	\
		base = kaddr + offset % PAGE_SIZE;		\
		len = min(min(n, (size_t)(p->bv_len - skip)),	\
		     (size_t)(PAGE_SIZE - offset % PAGE_SIZE));	\
		left = (STEP);					\
		kunmap_local(kaddr);				\
		len -= left;					\
		off += len;					\
		skip += len;					\
		if (skip == p->bv_len) {			\
			skip = 0;				\
			p++;					\
		}						\
		n -= len;					\
		if (left)					\
			break;					\
	}							\
	i->iov_offset = skip;					\
	n = off;						\
}

#define iterate_xarray(i, n, base, len, __off, STEP) {		\
	__label__ __out;					\
	size_t __off = 0;					\
	struct folio *folio;					\
	loff_t start = i->xarray_start + i->iov_offset;		\
	pgoff_t index = start / PAGE_SIZE;			\
	XA_STATE(xas, i->xarray, index);			\
								\
	len = PAGE_SIZE - offset_in_page(start);		\
	rcu_read_lock();					\
	xas_for_each(&xas, folio, ULONG_MAX) {			\
		unsigned left;					\
		size_t offset;					\
		if (xas_retry(&xas, folio))			\
			continue;				\
		if (WARN_ON(xa_is_value(folio)))		\
			break;					\
		if (WARN_ON(folio_test_hugetlb(folio)))		\
			break;					\
		offset = offset_in_folio(folio, start + __off);	\
		while (offset < folio_size(folio)) {		\
			base = kmap_local_folio(folio, offset);	\
			len = min(n, len);			\
			left = (STEP);				\
			kunmap_local(base);			\
			len -= left;				\
			__off += len;				\
			n -= len;				\
			if (left || n == 0)			\
				goto __out;			\
			offset += len;				\
			len = PAGE_SIZE;			\
		}						\
	}							\
__out:								\
	rcu_read_unlock();					\
	i->iov_offset += __off;					\
	n = __off;						\
}

#define __iterate_and_advance(i, n, base, len, off, I, K) {	\
	if (unlikely(i->count < n))				\
		n = i->count;					\
	if (likely(n)) {					\
		if (likely(iter_is_iovec(i))) {			\
			const struct iovec *iov = i->iov;	\
			void __user *base;			\
			size_t len;				\
			iterate_iovec(i, n, base, len, off,	\
						iov, (I))	\
			i->nr_segs -= iov - i->iov;		\
			i->iov = iov;				\
		} else if (iov_iter_is_bvec(i)) {		\
			const struct bio_vec *bvec = i->bvec;	\
			void *base;				\
			size_t len;				\
			iterate_bvec(i, n, base, len, off,	\
						bvec, (K))	\
			i->nr_segs -= bvec - i->bvec;		\
			i->bvec = bvec;				\
		} else if (iov_iter_is_kvec(i)) {		\
			const struct kvec *kvec = i->kvec;	\
			void *base;				\
			size_t len;				\
			iterate_iovec(i, n, base, len, off,	\
						kvec, (K))	\
			i->nr_segs -= kvec - i->kvec;		\
			i->kvec = kvec;				\
		} else if (iov_iter_is_xarray(i)) {		\
			void *base;				\
			size_t len;				\
			iterate_xarray(i, n, base, len, off,	\
							(K))	\
		}						\
		i->count -= n;					\
	}							\
}
#define iterate_and_advance(i, n, base, len, off, I, K) \
	__iterate_and_advance(i, n, base, len, off, I, ((void)(K),0))

static int copyout(void __user *to, const void *from, size_t n)
{
	if (should_fail_usercopy())
		return n;
	if (access_ok(to, n)) {
		instrument_copy_to_user(to, from, n);
		n = raw_copy_to_user(to, from, n);
	}
	return n;
}

static int copyin(void *to, const void __user *from, size_t n)
{
	if (should_fail_usercopy())
		return n;
	if (access_ok(from, n)) {
		instrument_copy_from_user(to, from, n);
		n = raw_copy_from_user(to, from, n);
	}
	return n;
}

static size_t copy_page_to_iter_iovec(struct page *page, size_t offset, size_t bytes,
			 struct iov_iter *i)
{
	size_t skip, copy, left, wanted;
	const struct iovec *iov;
	char __user *buf;
	void *kaddr, *from;

	if (unlikely(bytes > i->count))
		bytes = i->count;

	if (unlikely(!bytes))
		return 0;

	might_fault();
	wanted = bytes;
	iov = i->iov;
	skip = i->iov_offset;
	buf = iov->iov_base + skip;
	copy = min(bytes, iov->iov_len - skip);

	if (IS_ENABLED(CONFIG_HIGHMEM) && !fault_in_writeable(buf, copy)) {
		kaddr = kmap_atomic(page);
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
		if (likely(!bytes)) {
			kunmap_atomic(kaddr);
			goto done;
		}
		offset = from - kaddr;
		buf += copy;
		kunmap_atomic(kaddr);
		copy = min(bytes, iov->iov_len - skip);
	}
	 

	kaddr = kmap(page);
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

done:
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

void iov_iter_init(struct iov_iter *i, unsigned int direction,
			const struct iovec *iov, unsigned long nr_segs,
			size_t count)
{
	WARN_ON(direction & ~(READ | WRITE));
	*i = (struct iov_iter) {
		.iter_type = ITER_IOVEC,
		.nofault = false,
		.data_source = direction,
		.iov = iov,
		.nr_segs = nr_segs,
		.iov_offset = 0,
		.count = count
	};
}

size_t _copy_to_iter(const void *addr, size_t bytes, struct iov_iter *i)
{
	if (iter_is_iovec(i))
		might_fault();
	iterate_and_advance(i, bytes, base, len, off,
		copyout(base, addr + off, len),
		memcpy(base, addr + off, len)
	)

	return bytes;
}


size_t _copy_from_iter(void *addr, size_t bytes, struct iov_iter *i)
{
	if (iter_is_iovec(i))
		might_fault();
	iterate_and_advance(i, bytes, base, len, off,
		copyin(addr + off, base, len),
		memcpy(addr + off, base, len)
	)

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

static size_t __copy_page_to_iter(struct page *page, size_t offset, size_t bytes,
			 struct iov_iter *i)
{
	if (likely(iter_is_iovec(i)))
		return copy_page_to_iter_iovec(page, offset, bytes, i);
	if (iov_iter_is_bvec(i) || iov_iter_is_kvec(i) || iov_iter_is_xarray(i)) {
		void *kaddr = kmap_local_page(page);
		size_t wanted = _copy_to_iter(kaddr + offset, bytes, i);
		kunmap_local(kaddr);
		return wanted;
	}
	if (unlikely(iov_iter_is_discard(i))) {
		if (unlikely(i->count < bytes))
			bytes = i->count;
		i->count -= bytes;
		return bytes;
	}
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
		size_t n = __copy_page_to_iter(page, offset,
				min(bytes, (size_t)PAGE_SIZE - offset), i);
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

size_t iov_iter_zero(size_t bytes, struct iov_iter *i)
{
	iterate_and_advance(i, bytes, base, len, count,
		clear_user(base, len),
		memset(base, 0, len)
	)

	return bytes;
}

size_t copy_page_from_iter_atomic(struct page *page, unsigned offset, size_t bytes,
				  struct iov_iter *i)
{
	char *kaddr = kmap_atomic(page), *p = kaddr + offset;
	if (unlikely(!page_copy_sane(page, offset, bytes))) {
		kunmap_atomic(kaddr);
		return 0;
	}
	if (unlikely(iov_iter_is_discard(i))) {
		kunmap_atomic(kaddr);
		WARN_ON(1);
		return 0;
	}
	iterate_and_advance(i, bytes, base, len, off,
		copyin(p + off, base, len),
		memcpy(p + off, base, len)
	)
	kunmap_atomic(kaddr);
	return bytes;
}

static void iov_iter_bvec_advance(struct iov_iter *i, size_t size)
{
	struct bvec_iter bi;

	bi.bi_size = i->count;
	bi.bi_bvec_done = i->iov_offset;
	bi.bi_idx = 0;
	bvec_iter_advance(i->bvec, &bi, size);

	i->bvec += bi.bi_idx;
	i->nr_segs -= bi.bi_idx;
	i->count = bi.bi_size;
	i->iov_offset = bi.bi_bvec_done;
}

static void iov_iter_iovec_advance(struct iov_iter *i, size_t size)
{
	const struct iovec *iov, *end;

	if (!i->count)
		return;
	i->count -= size;

	size += i->iov_offset;  
	for (iov = i->iov, end = iov + i->nr_segs; iov < end; iov++) {
		if (likely(size < iov->iov_len))
			break;
		size -= iov->iov_len;
	}
	i->iov_offset = size;
	i->nr_segs -= iov - i->iov;
	i->iov = iov;
}

void iov_iter_advance(struct iov_iter *i, size_t size)
{
	if (unlikely(i->count < size))
		size = i->count;
	if (likely(iter_is_iovec(i) || iov_iter_is_kvec(i))) {
		 
		iov_iter_iovec_advance(i, size);
	} else if (iov_iter_is_bvec(i)) {
		iov_iter_bvec_advance(i, size);
	} else if (unlikely(iov_iter_is_xarray(i))) {
		i->iov_offset += size;
		i->count -= size;
	} else if (iov_iter_is_discard(i)) {
		i->count -= size;
	}
}

void iov_iter_revert(struct iov_iter *i, size_t unroll)
{
	if (!unroll)
		return;
	if (WARN_ON(unroll > MAX_RW_COUNT))
		return;
	i->count += unroll;
	if (unlikely(iov_iter_is_discard(i)))
		return;
	if (unroll <= i->iov_offset) {
		i->iov_offset -= unroll;
		return;
	}
	unroll -= i->iov_offset;
	if (iov_iter_is_xarray(i)) {
		BUG();  
	} else if (iov_iter_is_bvec(i)) {
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
			const struct kvec *kvec, unsigned long nr_segs,
			size_t count)
{
	WARN_ON(direction & ~(READ | WRITE));
	*i = (struct iov_iter){
		.iter_type = ITER_KVEC,
		.data_source = direction,
		.kvec = kvec,
		.nr_segs = nr_segs,
		.iov_offset = 0,
		.count = count
	};
}


/* iov_iter_pipe, iov_iter_xarray, iov_iter_discard, iov_iter_alignment,
   iov_iter_gap_alignment, iov_iter_get_pages, iov_iter_get_pages_alloc,
   csum_and_copy_*, hash_and_copy_*, iov_iter_npages removed - unused */

/* dup_iter, iovec_from_user, __import_iovec, import_iovec,
   import_single_range, iov_iter_restore removed - unused */
