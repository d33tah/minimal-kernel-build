#ifndef _LINUX_FS_CONTEXT_H
#define _LINUX_FS_CONTEXT_H

struct cred;
struct dentry;
struct file_system_type;
struct super_block;
struct user_namespace;

struct fs_context {
	const struct fs_context_operations *ops;
	struct dentry *root;
	struct user_namespace *user_ns;
	const struct cred *cred;
};

struct fs_context_operations {
	int (*get_tree)(struct fs_context *fc);
};

extern struct fs_context *
fs_context_for_mount(struct file_system_type *fs_type);
extern void put_fs_context(struct fs_context *fc);

extern int get_tree_nodev(struct fs_context *fc,
			  int (*fill_super)(struct super_block *sb,
					    struct fs_context *fc));

#endif
