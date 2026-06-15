#include <linux/err.h>
#include <linux/bug.h>
#include <linux/atomic.h>
#include <linux/log2.h>
#include <linux/fs.h>


#define ERRSEQ_SHIFT		ilog2(MAX_ERRNO + 1)

#define ERRSEQ_SEEN		(1 << ERRSEQ_SHIFT)

errseq_t errseq_sample(errseq_t *eseq)
{
	errseq_t old = READ_ONCE(*eseq);

	 
	if (!(old & ERRSEQ_SEEN))
		old = 0;
	return old;
}
