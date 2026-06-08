#include <linux/err.h>
#include <linux/bug.h>
#include <linux/atomic.h>
#include <linux/log2.h>
#include <linux/fs.h>


#define ERRSEQ_SHIFT		ilog2(MAX_ERRNO + 1)

#define ERRSEQ_SEEN		(1 << ERRSEQ_SHIFT)

#define ERRSEQ_CTR_INC		(1 << (ERRSEQ_SHIFT + 1))

errseq_t errseq_set(errseq_t *eseq, int err)
{
	errseq_t cur, old;

	 
	BUILD_BUG_ON_NOT_POWER_OF_2(MAX_ERRNO + 1);

	 
	old = READ_ONCE(*eseq);

	if (WARN(unlikely(err == 0 || (unsigned int)-err > MAX_ERRNO),
				"err = %d\n", err))
		return old;

	for (;;) {
		errseq_t new;

		 
		new = (old & ~(MAX_ERRNO|ERRSEQ_SEEN)) | -err;

		 
		if (old & ERRSEQ_SEEN)
			new += ERRSEQ_CTR_INC;

		 
		if (new == old) {
			cur = new;
			break;
		}

		 
		cur = cmpxchg(eseq, old, new);

		 
		if (likely(cur == old || cur == new))
			break;

		 
		old = cur;
	}
	return cur;
}

errseq_t errseq_sample(errseq_t *eseq)
{
	errseq_t old = READ_ONCE(*eseq);

	 
	if (!(old & ERRSEQ_SEEN))
		old = 0;
	return old;
}
