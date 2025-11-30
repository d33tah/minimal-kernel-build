/* Minimal bio.h - most bio code unused in this minimal kernel */
#ifndef __LINUX_BIO_H
#define __LINUX_BIO_H

#include <linux/blk_types.h>

/* Forward declarations */
struct bio_set;
struct bio_integrity_payload;
struct folio_iter;
struct request_queue;

/* Minimal defines needed by other headers */
#define BIO_MAX_VECS		256U
#define bio_prio(bio)			(bio)->bi_ioprio
#define bio_set_prio(bio, prio)		((bio)->bi_ioprio = prio)
#define bio_data_dir(bio)	(op_is_write(bio_op(bio)) ? WRITE : READ)

enum bip_flags { BIP_LAST };

static inline void bio_set_flag(struct bio *bio, unsigned int bit)
{
	bio->bi_flags |= (1U << bit);
}

static inline void bio_clear_flag(struct bio *bio, unsigned int bit)
{
	bio->bi_flags &= ~(1U << bit);
}

static inline void bio_associate_blkg(struct bio *bio) { }

static inline void bio_set_dev(struct bio *bio, struct block_device *bdev)
{
	bio_clear_flag(bio, BIO_REMAPPED);
	if (bio->bi_bdev != bdev)
		bio_clear_flag(bio, BIO_THROTTLED);
	bio->bi_bdev = bdev;
	bio_associate_blkg(bio);
}

static inline void *bio_integrity(struct bio *bio)
{
	return NULL;
}

static inline bool bio_integrity_flagged(struct bio *bio, enum bip_flags flag)
{
	return false;
}

#endif /* __LINUX_BIO_H */
