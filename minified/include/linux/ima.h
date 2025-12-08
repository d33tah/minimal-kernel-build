/* --- 2025-12-08 03:50 --- Minimal ima.h - only used stubs */
#ifndef _LINUX_IMA_H
#define _LINUX_IMA_H

struct file;
struct user_namespace;
struct dentry;

/* Only the functions actually called from the codebase */
static inline void ima_file_free(struct file *file) {}
static inline void ima_inode_post_setattr(struct user_namespace *mnt_userns,
					  struct dentry *dentry) {}
static inline int ima_file_check(struct file *file, int mask) { return 0; }

#endif
