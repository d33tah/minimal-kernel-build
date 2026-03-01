
#ifndef _LINUX_FS_CONTEXT_H
#define _LINUX_FS_CONTEXT_H

#include <linux/kernel.h>
#include <linux/refcount.h>
#include <linux/errno.h>
#include <linux/mutex.h>
#include <linux/fs.h>

struct cred;
struct dentry;
struct file_system_type;
struct net;
struct super_block;
struct user_namespace;

enum fs_context_purpose {
	FS_CONTEXT_FOR_MOUNT,
};

enum fs_value_type {
	fs_value_is_flag = 1,
	fs_value_is_string,
};

struct fs_parameter {
	const char		*key;		 
	enum fs_value_type	type:8;		 
	union {
		char		*string;
		void		*blob;
		struct filename	*name;
		struct file	*file;
	};
	size_t	size;
	int	dirfd;
};

struct p_log {
	const char *prefix;
	struct fc_log *log;
};

struct fs_context {
	const struct fs_context_operations *ops;
	struct mutex		uapi_mutex;	 
	struct file_system_type	*fs_type;
	void			*fs_private;	 
	void			*sget_key;
	struct dentry		*root;		 
	struct user_namespace	*user_ns;	 
	struct net		*net_ns;	 
	const struct cred	*cred;		 
	struct p_log		log;		 
	const char		*source;	 
	void			*security;	 
	void			*s_fs_info;	 
	unsigned int		sb_flags;	 
	unsigned int		sb_flags_mask;	 
	unsigned int		s_iflags;	 
	unsigned int		lsm_flags;	 
	enum fs_context_purpose	purpose:8;
	bool			need_free:1;	 
	bool			global:1;	 
	bool			oldapi:1;	 
};

struct fs_context_operations {
	void (*free)(struct fs_context *fc);
	int (*parse_param)(struct fs_context *fc, struct fs_parameter *param);
	int (*parse_monolithic)(struct fs_context *fc, void *data);
	int (*get_tree)(struct fs_context *fc);
};

extern struct fs_context *fs_context_for_mount(struct file_system_type *fs_type,
						unsigned int sb_flags);

extern int vfs_parse_fs_param(struct fs_context *fc, struct fs_parameter *param);
extern int vfs_parse_fs_string(struct fs_context *fc, const char *key,
			       const char *value, size_t v_size);
extern int generic_parse_monolithic(struct fs_context *fc, void *data);
extern int vfs_get_tree(struct fs_context *fc);
extern void put_fs_context(struct fs_context *fc);
extern int vfs_parse_fs_param_source(struct fs_context *fc,
				     struct fs_parameter *param);

extern int get_tree_nodev(struct fs_context *fc,
			 int (*fill_super)(struct super_block *sb,
					   struct fs_context *fc));

struct fc_log {
	refcount_t	usage;
	u8		head;		 
	u8		tail;		 
	u8		need_free;	 
	struct module	*owner;		 
	char		*buffer[8];
};

extern __attribute__((format(printf, 4, 5)))
void logfc(struct fc_log *log, const char *prefix, char level, const char *fmt, ...);

#define __logfc(fc, l, fmt, ...) logfc((fc)->log.log, NULL, \
					l, fmt, ## __VA_ARGS__)
#define __plog(p, l, fmt, ...) logfc((p)->log, (p)->prefix, \
					l, fmt, ## __VA_ARGS__)

#define errorf(fc, fmt, ...) __logfc(fc, 'e', fmt, ## __VA_ARGS__)
#define error_plog(p, fmt, ...) __plog(p, 'e', fmt, ## __VA_ARGS__)

#define invalf(fc, fmt, ...) (errorf(fc, fmt, ## __VA_ARGS__), -EINVAL)
#define inval_plog(p, fmt, ...) (error_plog(p, fmt, ## __VA_ARGS__), -EINVAL)

#endif
