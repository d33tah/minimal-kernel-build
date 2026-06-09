 
 

struct super_block;
struct file_system_type;
struct iomap;
struct iomap_ops;
struct linux_binprm;
struct path;
struct mount;
struct fs_context;
struct user_namespace;
struct pipe_inode_info;

 
static inline void bdev_cache_init(void)
{
}
/* emergency_thaw_bdev removed - unused */

 
int __block_write_begin_int(struct folio *folio, loff_t pos, unsigned len,
		get_block_t *get_block, const struct iomap *iomap);

 
extern void __init chrdev_init(void);

 
extern const struct fs_context_operations legacy_fs_context_ops;
extern int parse_monolithic_mount_data(struct fs_context *, void *);

 
extern int filename_lookup(int dfd, struct filename *name, unsigned flags,
			   struct path *path, struct path *root);
int do_rmdir(int dfd, struct filename *name);
int do_unlinkat(int dfd, struct filename *name);
int may_linkat(struct user_namespace *mnt_userns, struct path *link);

 
extern struct vfsmount *lookup_mnt(const struct path *);

extern void __init mnt_init(void);

extern int __mnt_want_write_file(struct file *);
extern void __mnt_drop_write_file(struct file *);

extern void dissolve_on_fput(struct vfsmount *);
extern bool may_mount(void);

int path_mount(const char *dev_name, struct path *path,
		const char *type_page, unsigned long flags, void *data_page);

 
extern struct file *alloc_empty_file(int, const struct cred *);

/* reconfigure_super, user_get_super removed - unused */
void put_super(struct super_block *sb);
extern bool mount_capable(struct fs_context *);

 
struct open_flags {
	int open_flag;
	umode_t mode;
	int acc_mode;
	int intent;
	int lookup_flags;
};
extern struct file *do_filp_open(int dfd, struct filename *pathname,
		const struct open_flags *op);
extern struct open_how build_open_how(int flags, umode_t mode);
extern int build_open_flags(const struct open_how *how, struct open_flags *op);

long do_sys_ftruncate(unsigned int fd, loff_t length, int small);
int chmod_common(const struct path *path, umode_t mode);
/* do_fchownat declaration removed - made static */
int chown_common(const struct path *path, uid_t user, gid_t group);
extern int vfs_open(const struct path *, struct file *);

 
extern int dentry_needs_remove_privs(struct dentry *dentry);

 

 
extern int d_set_mounted(struct dentry *dentry);
extern struct dentry *d_alloc_cursor(struct dentry *);
extern struct dentry * d_alloc_pseudo(struct super_block *, const struct qstr *);
extern char *simple_dname(struct dentry *, char *, int);
extern void dput_to_list(struct dentry *, struct list_head *);
extern void shrink_dentry_list(struct list_head *);

 
extern const struct file_operations pipefifo_fops;

 
/* group_pin_kill removed - unused */
extern void mnt_pin_kill(struct mount *m);

 
extern const struct dentry_operations ns_dentry_operations;

 
int do_statx(int dfd, struct filename *filename, unsigned int flags,
	     unsigned int mask, struct statx __user *buffer);

/* splice_file_to_pipe removed - unused */

struct xattr_name {
	char name[XATTR_NAME_MAX + 1];
};

struct xattr_ctx {
	 
	union {
		const void __user *cvalue;
		void __user *value;
	};
	void *kvalue;
	size_t size;
	 
	struct xattr_name *kname;
	unsigned int flags;
};


ssize_t do_getxattr(struct user_namespace *mnt_userns,
		    struct dentry *d,
		    struct xattr_ctx *ctx);

int do_setxattr(struct user_namespace *mnt_userns, struct dentry *dentry,
		struct xattr_ctx *ctx);
