
#ifndef _LINUX_BUFFER_HEAD_H
#define _LINUX_BUFFER_HEAD_H

#include <linux/types.h>
#include <linux/fs.h>
#include <linux/linkage.h>
#include <linux/pagemap.h>
#include <linux/wait.h>
#include <linux/atomic.h>


static inline void buffer_init(void) {}
static inline bool try_to_free_buffers(struct folio *folio) { return true; }
static inline int inode_has_buffers(struct inode *inode) { return 0; }
static inline int sync_mapping_buffers(struct address_space *mapping) { return 0; }
#define buffer_heads_over_limit 0

#endif  
