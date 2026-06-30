 
 

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


/* emergency_thaw_bdev removed - unused */


extern void __init chrdev_init(void);


extern int parse_monolithic_mount_data(struct fs_context *, void *);

 
extern int filename_lookup(struct filename *name, unsigned flags,
			   struct path *path);
int do_rmdir(struct filename *name);
int do_unlinkat(struct filename *name);

 
extern void __init mnt_init(void);

extern int __mnt_want_write_file(struct file *);
extern void __mnt_drop_write_file(struct file *);


extern struct file *alloc_empty_file(int, const struct cred *);

/* reconfigure_super, user_get_super removed - unused */

 
struct open_flags {
	int open_flag;
	umode_t mode;
	int acc_mode;
	int intent;
	int lookup_flags;
};
extern struct file *do_filp_open(struct filename *pathname,
		const struct open_flags *op);
extern int build_open_flags(const struct open_how *how, struct open_flags *op);

int chmod_common(const struct path *path, umode_t mode);
/* do_fchownat declaration removed - made static */
int chown_common(const struct path *path, uid_t user, gid_t group);
extern int vfs_open(const struct path *, struct file *);





/* splice_file_to_pipe removed - unused */
