 
#ifndef _LINUX_COMPACTION_H
#define _LINUX_COMPACTION_H

 
enum compact_priority {
	COMPACT_PRIO_SYNC_FULL,
	MIN_COMPACT_PRIORITY = COMPACT_PRIO_SYNC_FULL,
	COMPACT_PRIO_SYNC_LIGHT,
	MIN_COMPACT_COSTLY_PRIORITY = COMPACT_PRIO_SYNC_LIGHT,
	DEF_COMPACT_PRIORITY = COMPACT_PRIO_SYNC_LIGHT,
	COMPACT_PRIO_ASYNC,
	INIT_COMPACT_PRIORITY = COMPACT_PRIO_ASYNC
};

 
 
enum compact_result {
	 
	COMPACT_NOT_SUITABLE_ZONE,
	 
	COMPACT_SKIPPED,
	 
	COMPACT_DEFERRED,

	 
	COMPACT_NO_SUITABLE_PAGE,
	 
	COMPACT_CONTINUE,

	 
	COMPACT_COMPLETE,
	 
	COMPACT_PARTIAL_SKIPPED,

	 
	COMPACT_CONTENDED,

	 
	COMPACT_SUCCESS,
};

struct alloc_context;  

 
static inline unsigned long compact_gap(unsigned int order)
{
	 
	return 2UL << order;
}

static inline void reset_isolation_suitable(pg_data_t *pgdat)
{
}

static inline enum compact_result compaction_suitable(struct zone *zone, int order,
					int alloc_flags, int highest_zoneidx)
{
	return COMPACT_SKIPPED;
}

static inline bool compaction_made_progress(enum compact_result result)
{
	return false;
}

static inline bool compaction_failed(enum compact_result result)
{
	return false;
}

static inline bool compaction_needs_reclaim(enum compact_result result)
{
	return false;
}

static inline bool compaction_withdrawn(enum compact_result result)
{
	return true;
}

static inline void kcompactd_run(int nid)
{
}
static inline void kcompactd_stop(int nid)
{
}

static inline void wakeup_kcompactd(pg_data_t *pgdat,
				int order, int highest_zoneidx)
{
}


struct node;

static inline int compaction_register_node(struct node *node)
{
	return 0;
}

static inline void compaction_unregister_node(struct node *node)
{
}

#endif  
