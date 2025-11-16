 
#ifndef _BLK_CGROUP_H
#define _BLK_CGROUP_H
 

#include <linux/types.h>

struct bio;
struct cgroup_subsys_state;
struct request_queue;

#define FC_APPID_LEN              129


#define blkcg_root_css	((struct cgroup_subsys_state *)ERR_PTR(-EINVAL))

static inline void blkcg_maybe_throttle_current(void) { }
static inline bool blk_cgroup_congested(void) { return false; }
static inline void blkcg_schedule_throttle(struct request_queue *q, bool use_memdelay) { }
static inline struct cgroup_subsys_state *bio_blkcg_css(struct bio *bio)
{
	return NULL;
}

int blkcg_set_fc_appid(char *app_id, u64 cgrp_id, size_t app_id_len);
char *blkcg_get_fc_appid(struct bio *bio);

#endif	 
