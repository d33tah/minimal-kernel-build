#include <linux/uio.h>
#include <linux/pagemap.h>
#include <linux/highmem.h>
#include <linux/uaccess.h>

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

static size_t __copy_page_to_iter(struct page *page, size_t offset,
				  size_t bytes, struct iov_iter *i)
{
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

size_t copy_page_to_iter(struct page *page, size_t offset, size_t bytes,
			 struct iov_iter *i)
{
	return __copy_page_to_iter(page, offset, bytes, i);
}

void iov_iter_kvec(struct iov_iter *i, unsigned int direction,
		   const struct kvec *kvec, unsigned long nr_segs, size_t count)
{
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
