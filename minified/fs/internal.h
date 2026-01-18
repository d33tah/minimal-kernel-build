 
 

struct super_block;
struct file_system_type;
/* struct iomap and struct iomap_ops forward decls removed - never defined */
struct linux_binprm;
struct path;
struct mount;
struct shrink_control;
struct fs_context;
struct user_namespace;
struct pipe_inode_info;

/* __block_write_begin_int removed - never defined or called */

 
extern void __init chrdev_init(void);

 
extern const struct fs_context_operations legacy_fs_context_ops;
extern int parse_monolithic_mount_data(struct fs_context *, void *);
/* vfs_clean_context, finish_clean_context removed - unused */

 
extern int filename_lookup(int dfd, struct filename *name, unsigned flags,
			   struct path *path, struct path *root);
extern int vfs_path_lookup(struct dentry *, struct vfsmount *,
			   const char *, unsigned int, struct path *);
/* do_rmdir, do_unlinkat, may_linkat, do_renameat2, do_mkdirat, do_symlinkat, do_linkat removed */

extern struct vfsmount *lookup_mnt(const struct path *);
/* finish_automount, sb_prepare_remount_readonly removed - unused */

extern void __init mnt_init(void);

/* __mnt_want_write_file, __mnt_drop_write_file removed - never called */

extern void dissolve_on_fput(struct vfsmount *);
extern bool may_mount(void);

int path_mount(const char *dev_name, struct path *path,
		const char *type_page, unsigned long flags, void *data_page);
/* path_umount, chroot_fs_refs removed - unused */

 
extern struct file *alloc_empty_file(int, const struct cred *);

/* reconfigure_super, user_get_super, mount_capable removed - unused */
extern bool trylock_super(struct super_block *sb);
void put_super(struct super_block *sb);


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

/* chmod_common, do_fchownat, chown_common removed - never called */
extern int vfs_open(const struct path *, struct file *);

 
extern long prune_icache_sb(struct super_block *sb, struct shrink_control *sc);

/* dentry_needs_remove_privs, get_nr_dirty_inodes, invalidate_inodes removed - unused */

 
extern int d_set_mounted(struct dentry *dentry);
extern long prune_dcache_sb(struct super_block *sb, struct shrink_control *sc);
extern struct dentry *d_alloc_cursor(struct dentry *);
extern struct dentry * d_alloc_pseudo(struct super_block *, const struct qstr *);
extern char *simple_dname(struct dentry *, char *, int);
extern void dput_to_list(struct dentry *, struct list_head *);
extern void shrink_dentry_list(struct list_head *);

/* pipefifo_fops moved to inode.c */

/* group_pin_kill removed - unused */
extern void mnt_pin_kill(struct mount *m);

/* ns_dentry_operations, sb_init_dio_done_wq removed - unused */
