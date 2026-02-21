#include <linux/uio.h>
#include <linux/pagemap.h>
#include <linux/highmem.h>
#include <linux/uaccess.h>

typedef __u16 __sum16;
typedef __u32 __wsum;

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
	if (access_ok(to, n))
		n = raw_copy_to_user(to, from, n);
	return n;
}

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

size_t _copy_to_iter(const void *addr, size_t bytes, struct iov_iter *i)
{
	iterate_and_advance(i, bytes, base, len, off,
			    copyout(base, addr + off, len),
			    memcpy(base, addr + off, len))

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

unsigned long _copy_to_user(void __user *to, const void *from, unsigned long n)
{
	if (likely(access_ok(to, n)))
		n = raw_copy_to_user(to, from, n);
	return n;
}
