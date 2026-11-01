struct path;

extern void __init mnt_init(void);

extern struct file *alloc_empty_file(int, const struct cred *);

struct open_flags {
	int open_flag;
	umode_t mode;
	int acc_mode, lookup_flags;
};
extern struct file *do_filp_open(struct filename *pathname,
				 const struct open_flags *op);

extern int vfs_open(const struct path *, struct file *);
