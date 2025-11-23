 
 
#include <linux/export.h>
#include <linux/bvec.h>
#include <linux/fault-inject-usercopy.h>
#include <linux/uio.h>
#include <linux/pagemap.h>
#include <linux/highmem.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/splice.h>
#include <linux/compat.h>
#include <net/checksum.h>
#include <linux/scatterlist.h>
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

static size_t copy_page_from_iter_iovec(struct page *page, size_t offset, size_t bytes,
			 struct iov_iter *i)
{
	size_t skip, copy, left, wanted;
	const struct iovec *iov;
	char __user *buf;
	void *kaddr, *to;

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

	if (IS_ENABLED(CONFIG_HIGHMEM) && !fault_in_readable(buf, copy)) {
		kaddr = kmap_atomic(page);
		to = kaddr + offset;

		 
		left = copyin(to, buf, copy);
		copy -= left;
		skip += copy;
		to += copy;
		bytes -= copy;

		while (unlikely(!left && bytes)) {
			iov++;
			buf = iov->iov_base;
			copy = min(bytes, iov->iov_len);
			left = copyin(to, buf, copy);
			copy -= left;
			skip = copy;
			to += copy;
			bytes -= copy;
		}
		if (likely(!bytes)) {
			kunmap_atomic(kaddr);
			goto done;
		}
		offset = to - kaddr;
		buf += copy;
		kunmap_atomic(kaddr);
		copy = min(bytes, iov->iov_len - skip);
	}
	 

	kaddr = kmap(page);
	to = kaddr + offset;
	left = copyin(to, buf, copy);
	copy -= left;
	skip += copy;
	to += copy;
	bytes -= copy;
	while (unlikely(!left && bytes)) {
		iov++;
		buf = iov->iov_base;
		copy = min(bytes, iov->iov_len);
		left = copyin(to, buf, copy);
		copy -= left;
		skip = copy;
		to += copy;
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

#ifdef PIPE_PARANOIA
static bool sanity(const struct iov_iter *i)
{
	struct pipe_inode_info *pipe = i->pipe;
	unsigned int p_head = pipe->head;
	unsigned int p_tail = pipe->tail;
	unsigned int p_mask = pipe->ring_size - 1;
	unsigned int p_occupancy = pipe_occupancy(p_head, p_tail);
	unsigned int i_head = i->head;
	unsigned int idx;

	if (i->iov_offset) {
		struct pipe_buffer *p;
		if (unlikely(p_occupancy == 0))
			goto Bad;	 
		if (unlikely(i_head != p_head - 1))
			goto Bad;	 

		p = &pipe->bufs[i_head & p_mask];
		if (unlikely(p->offset + p->len != i->iov_offset))
			goto Bad;	 
	} else {
		if (i_head != p_head)
			goto Bad;	 
	}
	return true;
Bad:
	printk(KERN_ERR "idx = %d, offset = %zd\n", i_head, i->iov_offset);
	printk(KERN_ERR "head = %d, tail = %d, buffers = %d\n",
			p_head, p_tail, pipe->ring_size);
	for (idx = 0; idx < pipe->ring_size; idx++)
		printk(KERN_ERR "[%p %p %d %d]\n",
			pipe->bufs[idx].ops,
			pipe->bufs[idx].page,
			pipe->bufs[idx].offset,
			pipe->bufs[idx].len);
	WARN_ON(1);
	return false;
}
#else
#define sanity(i) true
#endif

static size_t copy_page_to_iter_pipe(struct page *page, size_t offset, size_t bytes,
			 struct iov_iter *i)
{
	struct pipe_inode_info *pipe = i->pipe;
	struct pipe_buffer *buf;
	unsigned int p_tail = pipe->tail;
	unsigned int p_mask = pipe->ring_size - 1;
	unsigned int i_head = i->head;
	size_t off;

	if (unlikely(bytes > i->count))
		bytes = i->count;

	if (unlikely(!bytes))
		return 0;

	if (!sanity(i))
		return 0;

	off = i->iov_offset;
	buf = &pipe->bufs[i_head & p_mask];
	if (off) {
		if (offset == off && buf->page == page) {
			 
			buf->len += bytes;
			i->iov_offset += bytes;
			goto out;
		}
		i_head++;
		buf = &pipe->bufs[i_head & p_mask];
	}
	if (pipe_full(i_head, p_tail, pipe->max_usage))
		return 0;

	buf->ops = &page_cache_pipe_buf_ops;
	buf->flags = 0;
	get_page(page);
	buf->page = page;
	buf->offset = offset;
	buf->len = bytes;

	pipe->head = i_head + 1;
	i->iov_offset = offset + bytes;
	i->head = i_head;
out:
	i->count -= bytes;
	return bytes;
}

/* Stubbed fault_in functions - not used externally */
size_t fault_in_iov_iter_readable(const struct iov_iter *i, size_t size)
{
	return 0;
}

size_t fault_in_iov_iter_writeable(const struct iov_iter *i, size_t size)
{
	return 0;
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

static inline bool allocated(struct pipe_buffer *buf)
{
	return buf->ops == &default_pipe_buf_ops;
}

static inline void data_start(const struct iov_iter *i,
			      unsigned int *iter_headp, size_t *offp)
{
	unsigned int p_mask = i->pipe->ring_size - 1;
	unsigned int iter_head = i->head;
	size_t off = i->iov_offset;

	if (off && (!allocated(&i->pipe->bufs[iter_head & p_mask]) ||
		    off == PAGE_SIZE)) {
		iter_head++;
		off = 0;
	}
	*iter_headp = iter_head;
	*offp = off;
}

static size_t push_pipe(struct iov_iter *i, size_t size,
			int *iter_headp, size_t *offp)
{
	struct pipe_inode_info *pipe = i->pipe;
	unsigned int p_tail = pipe->tail;
	unsigned int p_mask = pipe->ring_size - 1;
	unsigned int iter_head;
	size_t off;
	ssize_t left;

	if (unlikely(size > i->count))
		size = i->count;
	if (unlikely(!size))
		return 0;

	left = size;
	data_start(i, &iter_head, &off);
	*iter_headp = iter_head;
	*offp = off;
	if (off) {
		left -= PAGE_SIZE - off;
		if (left <= 0) {
			pipe->bufs[iter_head & p_mask].len += size;
			return size;
		}
		pipe->bufs[iter_head & p_mask].len = PAGE_SIZE;
		iter_head++;
	}
	while (!pipe_full(iter_head, p_tail, pipe->max_usage)) {
		struct pipe_buffer *buf = &pipe->bufs[iter_head & p_mask];
		struct page *page = alloc_page(GFP_USER);
		if (!page)
			break;

		buf->ops = &default_pipe_buf_ops;
		buf->flags = 0;
		buf->page = page;
		buf->offset = 0;
		buf->len = min_t(ssize_t, left, PAGE_SIZE);
		left -= buf->len;
		iter_head++;
		pipe->head = iter_head;

		if (left == 0)
			return size;
	}
	return size - left;
}

static size_t copy_pipe_to_iter(const void *addr, size_t bytes,
				struct iov_iter *i)
{
	struct pipe_inode_info *pipe = i->pipe;
	unsigned int p_mask = pipe->ring_size - 1;
	unsigned int i_head;
	size_t n, off;

	if (!sanity(i))
		return 0;

	bytes = n = push_pipe(i, bytes, &i_head, &off);
	if (unlikely(!n))
		return 0;
	do {
		size_t chunk = min_t(size_t, n, PAGE_SIZE - off);
		memcpy_to_page(pipe->bufs[i_head & p_mask].page, off, addr, chunk);
		i->head = i_head;
		i->iov_offset = off + chunk;
		n -= chunk;
		addr += chunk;
		off = 0;
		i_head++;
	} while (n);
	i->count -= bytes;
	return bytes;
}

size_t _copy_to_iter(const void *addr, size_t bytes, struct iov_iter *i)
{
	if (unlikely(iov_iter_is_pipe(i)))
		return copy_pipe_to_iter(addr, bytes, i);
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
	if (unlikely(iov_iter_is_pipe(i))) {
		WARN_ON(1);
		return 0;
	}
	if (iter_is_iovec(i))
		might_fault();
	iterate_and_advance(i, bytes, base, len, off,
		copyin(addr + off, base, len),
		memcpy(addr + off, base, len)
	)

	return bytes;
}

size_t _copy_from_iter_nocache(void *addr, size_t bytes, struct iov_iter *i)
{
	if (unlikely(iov_iter_is_pipe(i))) {
		WARN_ON(1);
		return 0;
	}
	iterate_and_advance(i, bytes, base, len, off,
		__copy_from_user_inatomic_nocache(addr + off, base, len),
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
	if (iov_iter_is_pipe(i))
		return copy_page_to_iter_pipe(page, offset, bytes, i);
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

size_t copy_page_from_iter(struct page *page, size_t offset, size_t bytes,
			 struct iov_iter *i)
{
	if (unlikely(!page_copy_sane(page, offset, bytes)))
		return 0;
	if (likely(iter_is_iovec(i)))
		return copy_page_from_iter_iovec(page, offset, bytes, i);
	if (iov_iter_is_bvec(i) || iov_iter_is_kvec(i) || iov_iter_is_xarray(i)) {
		void *kaddr = kmap_local_page(page);
		size_t wanted = _copy_from_iter(kaddr + offset, bytes, i);
		kunmap_local(kaddr);
		return wanted;
	}
	WARN_ON(1);
	return 0;
}

static size_t pipe_zero(size_t bytes, struct iov_iter *i)
{
	struct pipe_inode_info *pipe = i->pipe;
	unsigned int p_mask = pipe->ring_size - 1;
	unsigned int i_head;
	size_t n, off;

	if (!sanity(i))
		return 0;

	bytes = n = push_pipe(i, bytes, &i_head, &off);
	if (unlikely(!n))
		return 0;

	do {
		size_t chunk = min_t(size_t, n, PAGE_SIZE - off);
		char *p = kmap_local_page(pipe->bufs[i_head & p_mask].page);
		memset(p + off, 0, chunk);
		kunmap_local(p);
		i->head = i_head;
		i->iov_offset = off + chunk;
		n -= chunk;
		off = 0;
		i_head++;
	} while (n);
	i->count -= bytes;
	return bytes;
}

size_t iov_iter_zero(size_t bytes, struct iov_iter *i)
{
	if (unlikely(iov_iter_is_pipe(i)))
		return pipe_zero(bytes, i);
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
	if (unlikely(iov_iter_is_pipe(i) || iov_iter_is_discard(i))) {
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

static inline void pipe_truncate(struct iov_iter *i)
{
	struct pipe_inode_info *pipe = i->pipe;
	unsigned int p_tail = pipe->tail;
	unsigned int p_head = pipe->head;
	unsigned int p_mask = pipe->ring_size - 1;

	if (!pipe_empty(p_head, p_tail)) {
		struct pipe_buffer *buf;
		unsigned int i_head = i->head;
		size_t off = i->iov_offset;

		if (off) {
			buf = &pipe->bufs[i_head & p_mask];
			buf->len = off - buf->offset;
			i_head++;
		}
		while (p_head != i_head) {
			p_head--;
			pipe_buf_release(pipe, &pipe->bufs[p_head & p_mask]);
		}

		pipe->head = p_head;
	}
}

static void pipe_advance(struct iov_iter *i, size_t size)
{
	struct pipe_inode_info *pipe = i->pipe;
	if (size) {
		struct pipe_buffer *buf;
		unsigned int p_mask = pipe->ring_size - 1;
		unsigned int i_head = i->head;
		size_t off = i->iov_offset, left = size;

		if (off)  
			left += off - pipe->bufs[i_head & p_mask].offset;
		while (1) {
			buf = &pipe->bufs[i_head & p_mask];
			if (left <= buf->len)
				break;
			left -= buf->len;
			i_head++;
		}
		i->head = i_head;
		i->iov_offset = buf->offset + left;
	}
	i->count -= size;
	 
	pipe_truncate(i);
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
	} else if (iov_iter_is_pipe(i)) {
		pipe_advance(i, size);
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
	if (unlikely(iov_iter_is_pipe(i))) {
		struct pipe_inode_info *pipe = i->pipe;
		unsigned int p_mask = pipe->ring_size - 1;
		unsigned int i_head = i->head;
		size_t off = i->iov_offset;
		while (1) {
			struct pipe_buffer *b = &pipe->bufs[i_head & p_mask];
			size_t n = off - b->offset;
			if (unroll < n) {
				off -= unroll;
				break;
			}
			unroll -= n;
			if (!unroll && i_head == i->start_head) {
				off = 0;
				break;
			}
			i_head--;
			b = &pipe->bufs[i_head & p_mask];
			off = b->offset + b->len;
		}
		i->iov_offset = off;
		i->head = i_head;
		pipe_truncate(i);
		return;
	}
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

/* Stubbed - not used externally */
size_t iov_iter_single_seg_count(const struct iov_iter *i)
{
	return i->count;
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

/* Stubbed - not used externally */
void iov_iter_bvec(struct iov_iter *i, unsigned int direction,
			const struct bio_vec *bvec, unsigned long nr_segs,
			size_t count)
{
}

/* Stubbed iov_iter init functions - not used externally */
void iov_iter_pipe(struct iov_iter *i, unsigned int direction,
			struct pipe_inode_info *pipe,
			size_t count)
{
}

void iov_iter_xarray(struct iov_iter *i, unsigned int direction,
		     struct xarray *xarray, loff_t start, size_t count)
{
}

void iov_iter_discard(struct iov_iter *i, unsigned int direction, size_t count)
{
}

static unsigned long iov_iter_alignment_iovec(const struct iov_iter *i)
{
	unsigned long res = 0;
	size_t size = i->count;
	size_t skip = i->iov_offset;
	unsigned k;

	for (k = 0; k < i->nr_segs; k++, skip = 0) {
		size_t len = i->iov[k].iov_len - skip;
		if (len) {
			res |= (unsigned long)i->iov[k].iov_base + skip;
			if (len > size)
				len = size;
			res |= len;
			size -= len;
			if (!size)
				break;
		}
	}
	return res;
}

static unsigned long iov_iter_alignment_bvec(const struct iov_iter *i)
{
	unsigned res = 0;
	size_t size = i->count;
	unsigned skip = i->iov_offset;
	unsigned k;

	for (k = 0; k < i->nr_segs; k++, skip = 0) {
		size_t len = i->bvec[k].bv_len - skip;
		res |= (unsigned long)i->bvec[k].bv_offset + skip;
		if (len > size)
			len = size;
		res |= len;
		size -= len;
		if (!size)
			break;
	}
	return res;
}

unsigned long iov_iter_alignment(const struct iov_iter *i)
{
	 
	if (likely(iter_is_iovec(i) || iov_iter_is_kvec(i)))
		return iov_iter_alignment_iovec(i);

	if (iov_iter_is_bvec(i))
		return iov_iter_alignment_bvec(i);

	if (iov_iter_is_pipe(i)) {
		unsigned int p_mask = i->pipe->ring_size - 1;
		size_t size = i->count;

		if (size && i->iov_offset && allocated(&i->pipe->bufs[i->head & p_mask]))
			return size | i->iov_offset;
		return size;
	}

	if (iov_iter_is_xarray(i))
		return (i->xarray_start + i->iov_offset) | i->count;

	return 0;
}

/* Stubbed - not used externally */
unsigned long iov_iter_gap_alignment(const struct iov_iter *i)
{
	return 0;
}

ssize_t iov_iter_get_pages(struct iov_iter *i,
		   struct page **pages, size_t maxsize, unsigned maxpages,
		   size_t *start)
{
	return -EFAULT;
}

ssize_t iov_iter_get_pages_alloc(struct iov_iter *i,
		   struct page ***pages, size_t maxsize,
		   size_t *start)
{
	return -EFAULT;
}

size_t csum_and_copy_from_iter(void *addr, size_t bytes, __wsum *csum,
			       struct iov_iter *i)
{
	return 0;
}

size_t csum_and_copy_to_iter(const void *addr, size_t bytes, void *_csstate,
			     struct iov_iter *i)
{
	return 0;
}

size_t hash_and_copy_to_iter(const void *addr, size_t bytes, void *hashp,
		struct iov_iter *i)
{
	return 0;
}

int iov_iter_npages(const struct iov_iter *i, int maxpages)
{
	return 0;
}

const void *dup_iter(struct iov_iter *new, struct iov_iter *old, gfp_t flags)
{
	return NULL;
}

static int copy_compat_iovec_from_user(struct iovec *iov,
		const struct iovec __user *uvec, unsigned long nr_segs)
{
	const struct compat_iovec __user *uiov =
		(const struct compat_iovec __user *)uvec;
	int ret = -EFAULT, i;

	if (!user_access_begin(uiov, nr_segs * sizeof(*uiov)))
		return -EFAULT;

	for (i = 0; i < nr_segs; i++) {
		compat_uptr_t buf;
		compat_ssize_t len;

		unsafe_get_user(len, &uiov[i].iov_len, uaccess_end);
		unsafe_get_user(buf, &uiov[i].iov_base, uaccess_end);

		 
		if (len < 0) {
			ret = -EINVAL;
			goto uaccess_end;
		}
		iov[i].iov_base = compat_ptr(buf);
		iov[i].iov_len = len;
	}

	ret = 0;
uaccess_end:
	user_access_end();
	return ret;
}

static int copy_iovec_from_user(struct iovec *iov,
		const struct iovec __user *uvec, unsigned long nr_segs)
{
	unsigned long seg;

	if (copy_from_user(iov, uvec, nr_segs * sizeof(*uvec)))
		return -EFAULT;
	for (seg = 0; seg < nr_segs; seg++) {
		if ((ssize_t)iov[seg].iov_len < 0)
			return -EINVAL;
	}

	return 0;
}

struct iovec *iovec_from_user(const struct iovec __user *uvec,
		unsigned long nr_segs, unsigned long fast_segs,
		struct iovec *fast_iov, bool compat)
{
	struct iovec *iov = fast_iov;
	int ret;

	 
	if (nr_segs == 0)
		return iov;
	if (nr_segs > UIO_MAXIOV)
		return ERR_PTR(-EINVAL);
	if (nr_segs > fast_segs) {
		iov = kmalloc_array(nr_segs, sizeof(struct iovec), GFP_KERNEL);
		if (!iov)
			return ERR_PTR(-ENOMEM);
	}

	if (compat)
		ret = copy_compat_iovec_from_user(iov, uvec, nr_segs);
	else
		ret = copy_iovec_from_user(iov, uvec, nr_segs);
	if (ret) {
		if (iov != fast_iov)
			kfree(iov);
		return ERR_PTR(ret);
	}

	return iov;
}

ssize_t __import_iovec(int type, const struct iovec __user *uvec,
		 unsigned nr_segs, unsigned fast_segs, struct iovec **iovp,
		 struct iov_iter *i, bool compat)
{
	ssize_t total_len = 0;
	unsigned long seg;
	struct iovec *iov;

	iov = iovec_from_user(uvec, nr_segs, fast_segs, *iovp, compat);
	if (IS_ERR(iov)) {
		*iovp = NULL;
		return PTR_ERR(iov);
	}

	 
	for (seg = 0; seg < nr_segs; seg++) {
		ssize_t len = (ssize_t)iov[seg].iov_len;

		if (!access_ok(iov[seg].iov_base, len)) {
			if (iov != *iovp)
				kfree(iov);
			*iovp = NULL;
			return -EFAULT;
		}

		if (len > MAX_RW_COUNT - total_len) {
			len = MAX_RW_COUNT - total_len;
			iov[seg].iov_len = len;
		}
		total_len += len;
	}

	iov_iter_init(i, type, iov, nr_segs, total_len);
	if (iov == *iovp)
		*iovp = NULL;
	else
		*iovp = iov;
	return total_len;
}

 
ssize_t import_iovec(int type, const struct iovec __user *uvec,
		 unsigned nr_segs, unsigned fast_segs,
		 struct iovec **iovp, struct iov_iter *i)
{
	return -EFAULT;
}

int import_single_range(int rw, void __user *buf, size_t len,
		 struct iovec *iov, struct iov_iter *i)
{
	return -EFAULT;
}

 
void iov_iter_restore(struct iov_iter *i, struct iov_iter_state *state)
{
	if (WARN_ON_ONCE(!iov_iter_is_bvec(i) && !iter_is_iovec(i)) &&
			 !iov_iter_is_kvec(i))
		return;
	i->iov_offset = state->iov_offset;
	i->count = state->count;
	 
	BUILD_BUG_ON(sizeof(struct iovec) != sizeof(struct kvec));
	if (iov_iter_is_bvec(i))
		i->bvec -= state->nr_segs - i->nr_segs;
	else
		i->iov -= state->nr_segs - i->nr_segs;
	i->nr_segs = state->nr_segs;
}
