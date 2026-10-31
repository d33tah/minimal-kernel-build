

struct path;
struct fs_context;

/* emergency_thaw_bdev removed - unused */

extern void __init mnt_init(void);

extern struct file *alloc_empty_file(int, const struct cred *);

/* reconfigure_super, user_get_super removed - unused */

struct open_flags {
	int open_flag;
	umode_t mode;
	int acc_mode, lookup_flags;
};
extern struct file *do_filp_open(struct filename *pathname,
				 const struct open_flags *op);

/* do_fchownat declaration removed - made static */
extern int vfs_open(const struct path *, struct file *);

/* splice_file_to_pipe removed - unused */
