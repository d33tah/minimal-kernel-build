 
struct super_block;
struct file_system_type;
struct linux_binprm;
struct path;
struct mount;
struct shrink_control;
struct fs_context;
struct user_namespace;

extern void __init chrdev_init(void);

extern int parse_monolithic_mount_data(struct fs_context *, void *);

extern void __init mnt_init(void);

extern struct file *alloc_empty_file(int, const struct cred *);

struct open_flags {
	int open_flag;
	umode_t mode;
	int acc_mode;
	int intent;
	int lookup_flags;
};
extern struct file *do_filp_open(int dfd, struct filename *pathname,
		const struct open_flags *op);

extern int vfs_open(const struct path *, struct file *);


