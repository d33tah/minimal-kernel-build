 
#ifndef _MM_PERCPU_INTERNAL_H
#define _MM_PERCPU_INTERNAL_H

#include <linux/types.h>
#include <linux/percpu.h>

 
struct pcpu_block_md {
	int			scan_hint;	 
	int			scan_hint_start;  
	int                     contig_hint;     
	int                     contig_hint_start;  
	int                     left_free;       
	int                     right_free;      
	int                     first_free;      
	int			nr_bits;	 
};

struct pcpu_chunk {

	struct list_head	list;		 
	int			free_bytes;	 
	struct pcpu_block_md	chunk_md;
	void			*base_addr;	 

	unsigned long		*alloc_map;	 
	unsigned long		*bound_map;	 
	struct pcpu_block_md	*md_blocks;	 

	void			*data;		 
	bool			immutable;	 
	bool			isolated;	 
	int			start_offset;	 
	int			end_offset;	 

	int			nr_pages;	 
	int			nr_populated;	 
	int                     nr_empty_pop_pages;  
	unsigned long		populated[];	 
};

extern spinlock_t pcpu_lock;

extern struct list_head *pcpu_chunk_lists;
extern int pcpu_nr_slots;
extern int pcpu_sidelined_slot;
extern int pcpu_to_depopulate_slot;
extern int pcpu_nr_empty_pop_pages;

extern struct pcpu_chunk *pcpu_first_chunk;
extern struct pcpu_chunk *pcpu_reserved_chunk;

 
static inline int pcpu_chunk_nr_blocks(struct pcpu_chunk *chunk)
{
	return chunk->nr_pages * PAGE_SIZE / PCPU_BITMAP_BLOCK_SIZE;
}

 
static inline int pcpu_nr_pages_to_map_bits(int pages)
{
	return pages * PAGE_SIZE / PCPU_MIN_ALLOC_SIZE;
}

 
static inline int pcpu_chunk_map_bits(struct pcpu_chunk *chunk)
{
	return pcpu_nr_pages_to_map_bits(chunk->nr_pages);
}

 
static inline size_t pcpu_obj_full_size(size_t size)
{
	size_t extra_size = 0;


	return size * num_possible_cpus() + extra_size;
}


static inline void pcpu_stats_save_ai(const struct pcpu_alloc_info *ai)
{
}

static inline void pcpu_stats_area_alloc(struct pcpu_chunk *chunk, size_t size)
{
}

static inline void pcpu_stats_area_dealloc(struct pcpu_chunk *chunk)
{
}

static inline void pcpu_stats_chunk_alloc(void)
{
}

static inline void pcpu_stats_chunk_dealloc(void)
{
}


#endif
