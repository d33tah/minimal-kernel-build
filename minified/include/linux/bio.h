 
 
#ifndef __LINUX_BIO_H
#define __LINUX_BIO_H

#include <linux/mempool.h>
 
#include <linux/blk_types.h>
#include <linux/uio.h>

#define BIO_MAX_VECS		256U


#define bio_prio(bio)			(bio)->bi_ioprio
#define bio_set_prio(bio, prio)		((bio)->bi_ioprio = prio)

#define bio_iter_iovec(bio, iter)				\
	bvec_iter_bvec((bio)->bi_io_vec, (iter))

#define bio_iter_page(bio, iter)				\
	bvec_iter_page((bio)->bi_io_vec, (iter))
#define bio_iter_len(bio, iter)					\
	bvec_iter_len((bio)->bi_io_vec, (iter))
#define bio_iter_offset(bio, iter)				\
	bvec_iter_offset((bio)->bi_io_vec, (iter))

#define bio_page(bio)		bio_iter_page((bio), (bio)->bi_iter)
#define bio_offset(bio)		bio_iter_offset((bio), (bio)->bi_iter)
#define bio_iovec(bio)		bio_iter_iovec((bio), (bio)->bi_iter)

#define bvec_iter_sectors(iter)	((iter).bi_size >> 9)
#define bvec_iter_end_sector(iter) ((iter).bi_sector + bvec_iter_sectors((iter)))

#define bio_sectors(bio)	bvec_iter_sectors((bio)->bi_iter)
#define bio_end_sector(bio)	bvec_iter_end_sector((bio)->bi_iter)

 
#define bio_data_dir(bio) \
	(op_is_write(bio_op(bio)) ? WRITE : READ)



void __bio_advance(struct bio *, unsigned bytes);


static inline void bio_advance_iter_single(const struct bio *bio,
					   struct bvec_iter *iter,
					   unsigned int bytes)
{
	iter->bi_sector += bytes >> 9;

	if (bio_op(bio) == REQ_OP_DISCARD ||
	    bio_op(bio) == REQ_OP_SECURE_ERASE ||
	    bio_op(bio) == REQ_OP_WRITE_ZEROES)
		iter->bi_size -= bytes;
	else
		bvec_iter_advance_single(bio->bi_io_vec, iter, bytes);
}

#define __bio_for_each_segment(bvl, bio, iter, start)			\
	for (iter = (start);						\
	     (iter).bi_size &&						\
		((bvl = bio_iter_iovec((bio), (iter))), 1);		\
	     bio_advance_iter_single((bio), &(iter), (bvl).bv_len))

#define bio_for_each_segment(bvl, bio, iter)				\
	__bio_for_each_segment(bvl, bio, iter, (bio)->bi_iter)

#define __bio_for_each_bvec(bvl, bio, iter, start)		\
	for (iter = (start);						\
	     (iter).bi_size &&						\
		((bvl = mp_bvec_iter_bvec((bio)->bi_io_vec, (iter))), 1); \
	     bio_advance_iter_single((bio), &(iter), (bvl).bv_len))

 
#define bio_for_each_bvec(bvl, bio, iter)			\
	__bio_for_each_bvec(bvl, bio, iter, (bio)->bi_iter)

 
#define bio_for_each_bvec_all(bvl, bio, i)		\
	for (i = 0, bvl = bio_first_bvec_all(bio);	\
	     i < (bio)->bi_vcnt; i++, bvl++)

#define bio_iter_last(bvec, iter) ((iter).bi_size == (bvec).bv_len)

static inline unsigned bio_segments(struct bio *bio)
{
	unsigned segs = 0;
	struct bio_vec bv;
	struct bvec_iter iter;

	 

	switch (bio_op(bio)) {
	case REQ_OP_DISCARD:
	case REQ_OP_SECURE_ERASE:
	case REQ_OP_WRITE_ZEROES:
		return 0;
	default:
		break;
	}

	bio_for_each_segment(bv, bio, iter)
		segs++;

	return segs;
}


 
struct folio_iter {
	struct folio *folio;
	size_t offset;
	size_t length;
	 
	struct folio *_next;
	size_t _seg_count;
	int _i;
};

enum bip_flags {
	BIP_BLOCK_INTEGRITY	= 1 << 0,  
	BIP_MAPPED_INTEGRITY	= 1 << 1,  
	BIP_CTRL_NOCHECK	= 1 << 2,  
	BIP_DISK_NOCHECK	= 1 << 3,  
	BIP_IP_CHECKSUM		= 1 << 4,  
};

 
struct bio_integrity_payload {
	struct bio		*bip_bio;	 

	struct bvec_iter	bip_iter;

	unsigned short		bip_vcnt;	 
	unsigned short		bip_max_vcnt;	 
	unsigned short		bip_flags;	 

	struct bvec_iter	bio_iter;	 

	struct work_struct	bip_work;	 

	struct bio_vec		*bip_vec;
	struct bio_vec		bip_inline_vecs[]; 
};

#if defined(CONFIG_BLK_DEV_INTEGRITY)

#endif  

void bio_trim(struct bio *bio, sector_t offset, sector_t size);
extern struct bio *bio_split(struct bio *bio, int sectors,
			     gfp_t gfp, struct bio_set *bs);


enum {
	BIOSET_NEED_BVECS = BIT(0),
	BIOSET_NEED_RESCUER = BIT(1),
	BIOSET_PERCPU_CACHE = BIT(2),
};
extern int bioset_init(struct bio_set *, unsigned int, unsigned int, int flags);
extern void bioset_exit(struct bio_set *);
extern int biovec_init_pool(mempool_t *pool, int pool_entries);

struct bio *bio_alloc_bioset(struct block_device *bdev, unsigned short nr_vecs,
			     unsigned int opf, gfp_t gfp_mask,
			     struct bio_set *bs);
struct bio *bio_kmalloc(unsigned short nr_vecs, gfp_t gfp_mask);
extern void bio_put(struct bio *);

struct bio *bio_alloc_clone(struct block_device *bdev, struct bio *bio_src,
		gfp_t gfp, struct bio_set *bs);
int bio_init_clone(struct block_device *bdev, struct bio *bio,
		struct bio *bio_src, gfp_t gfp);

extern struct bio_set fs_bio_set;

void submit_bio(struct bio *bio);

extern void bio_endio(struct bio *);

struct request_queue;

extern int submit_bio_wait(struct bio *bio);
void bio_init(struct bio *bio, struct block_device *bdev, struct bio_vec *table,
	      unsigned short max_vecs, unsigned int opf);
extern void bio_uninit(struct bio *);
void bio_reset(struct bio *bio, struct block_device *bdev, unsigned int opf);
void bio_chain(struct bio *, struct bio *);

int bio_add_page(struct bio *, struct page *, unsigned len, unsigned off);
bool bio_add_folio(struct bio *, struct folio *, size_t len, size_t off);
extern int bio_add_pc_page(struct request_queue *, struct bio *, struct page *,
			   unsigned int, unsigned int);
int bio_add_zone_append_page(struct bio *bio, struct page *page,
			     unsigned int len, unsigned int offset);
void __bio_add_page(struct bio *bio, struct page *page,
		unsigned int len, unsigned int off);
int bio_iov_iter_get_pages(struct bio *bio, struct iov_iter *iter);
void bio_iov_bvec_set(struct bio *bio, struct iov_iter *iter);
void __bio_release_pages(struct bio *bio, bool mark_dirty);
extern void bio_set_pages_dirty(struct bio *bio);
extern void bio_check_pages_dirty(struct bio *bio);

extern void bio_copy_data_iter(struct bio *dst, struct bvec_iter *dst_iter,
			       struct bio *src, struct bvec_iter *src_iter);
extern void bio_copy_data(struct bio *dst, struct bio *src);
extern void bio_free_pages(struct bio *bio);
void guard_bio_eod(struct bio *bio);
void zero_fill_bio(struct bio *bio);

#define bio_dev(bio) \
	disk_devt((bio)->bi_bdev->bd_disk)

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

 
struct bio_list {
	struct bio *head;
	struct bio *tail;
};

static inline int bio_list_empty(const struct bio_list *bl)
{
	return bl->head == NULL;
}

static inline void bio_list_init(struct bio_list *bl)
{
	bl->head = bl->tail = NULL;
}

#define BIO_EMPTY_LIST	{ NULL, NULL }

#define bio_list_for_each(bio, bl) \
	for (bio = (bl)->head; bio; bio = bio->bi_next)

static inline unsigned bio_list_size(const struct bio_list *bl)
{
	unsigned sz = 0;
	struct bio *bio;

	bio_list_for_each(bio, bl)
		sz++;

	return sz;
}

static inline void bio_list_add(struct bio_list *bl, struct bio *bio)
{
	bio->bi_next = NULL;

	if (bl->tail)
		bl->tail->bi_next = bio;
	else
		bl->head = bio;

	bl->tail = bio;
}

static inline void bio_list_add_head(struct bio_list *bl, struct bio *bio)
{
	bio->bi_next = bl->head;

	bl->head = bio;

	if (!bl->tail)
		bl->tail = bio;
}

static inline void bio_list_merge(struct bio_list *bl, struct bio_list *bl2)
{
	if (!bl2->head)
		return;

	if (bl->tail)
		bl->tail->bi_next = bl2->head;
	else
		bl->head = bl2->head;

	bl->tail = bl2->tail;
}

static inline void bio_list_merge_head(struct bio_list *bl,
				       struct bio_list *bl2)
{
	if (!bl2->head)
		return;

	if (bl->head)
		bl2->tail->bi_next = bl->head;
	else
		bl->tail = bl2->tail;

	bl->head = bl2->head;
}

static inline struct bio *bio_list_peek(struct bio_list *bl)
{
	return bl->head;
}

static inline struct bio *bio_list_pop(struct bio_list *bl)
{
	struct bio *bio = bl->head;

	if (bio) {
		bl->head = bl->head->bi_next;
		if (!bl->head)
			bl->tail = NULL;

		bio->bi_next = NULL;
	}

	return bio;
}

static inline struct bio *bio_list_get(struct bio_list *bl)
{
	struct bio *bio = bl->head;

	bl->head = bl->tail = NULL;

	return bio;
}

 
static inline void bio_inc_remaining(struct bio *bio)
{
	bio_set_flag(bio, BIO_CHAIN);
	smp_mb__before_atomic();
	atomic_inc(&bio->__bi_remaining);
}

 
#define BIO_POOL_SIZE 2

struct bio_set {
	struct kmem_cache *bio_slab;
	unsigned int front_pad;

	 
	struct bio_alloc_cache __percpu *cache;

	mempool_t bio_pool;
	mempool_t bvec_pool;
#if defined(CONFIG_BLK_DEV_INTEGRITY)
	mempool_t bio_integrity_pool;
	mempool_t bvec_integrity_pool;
#endif

	unsigned int back_pad;
	 
	spinlock_t		rescue_lock;
	struct bio_list		rescue_list;
	struct work_struct	rescue_work;
	struct workqueue_struct	*rescue_workqueue;

	 
	struct hlist_node cpuhp_dead;
};

static inline bool bioset_initialized(struct bio_set *bs)
{
	return bs->bio_slab != NULL;
}

#if defined(CONFIG_BLK_DEV_INTEGRITY)

#define bip_for_each_vec(bvl, bip, iter)				\
	for_each_bvec(bvl, (bip)->bip_vec, iter, (bip)->bip_iter)

#define bio_for_each_integrity_vec(_bvl, _bio, _iter)			\
	for_each_bio(_bio)						\
		bip_for_each_vec(_bvl, _bio->bi_integrity, _iter)

extern struct bio_integrity_payload *bio_integrity_alloc(struct bio *, gfp_t, unsigned int);
extern int bio_integrity_add_page(struct bio *, struct page *, unsigned int, unsigned int);
extern bool bio_integrity_prep(struct bio *);
extern void bio_integrity_advance(struct bio *, unsigned int);
extern void bio_integrity_trim(struct bio *);
extern int bio_integrity_clone(struct bio *, struct bio *, gfp_t);
extern int bioset_integrity_create(struct bio_set *, int);
extern void bioset_integrity_free(struct bio_set *);
extern void bio_integrity_init(void);

#else  

static inline void *bio_integrity(struct bio *bio)
{
	return NULL;
}

static inline int bioset_integrity_create(struct bio_set *bs, int pool_size)
{
	return 0;
}

static inline void bioset_integrity_free (struct bio_set *bs)
{
	return;
}

static inline bool bio_integrity_flagged(struct bio *bio, enum bip_flags flag)
{
	return false;
}

#endif  

 
static inline void bio_set_polled(struct bio *bio, struct kiocb *kiocb)
{
	bio->bi_opf |= REQ_POLLED;
	if (!is_sync_kiocb(kiocb))
		bio->bi_opf |= REQ_NOWAIT;
}

static inline void bio_clear_polled(struct bio *bio)
{
	 
	bio->bi_opf &= ~(REQ_POLLED | REQ_ALLOC_CACHE);
}

struct bio *blk_next_bio(struct bio *bio, struct block_device *bdev,
		unsigned int nr_pages, unsigned int opf, gfp_t gfp);

#endif  
