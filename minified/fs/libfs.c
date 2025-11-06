// SPDX-License-Identifier: GPL-2.0-only
/* Stubbed libfs.c */
#include <linux/fs.h>
#include <linux/pagemap.h>
#include <linux/export.h>
#include <linux/namei.h>
#include <linux/exportfs.h>
#include <linux/fs_context.h>
#include <linux/pseudo_fs.h>

int simple_getattr(struct user_namespace *mnt_userns, const struct path *path,
		   struct kstat *stat, u32 request_mask, unsigned int query_flags) { return 0; }
EXPORT_SYMBOL(simple_getattr);

int simple_statfs(struct dentry *dentry, struct kstatfs *buf) { return 0; }
EXPORT_SYMBOL(simple_statfs);

int always_delete_dentry(const struct dentry *dentry) { return 1; }
EXPORT_SYMBOL(always_delete_dentry);

const struct dentry_operations simple_dentry_operations = { };
EXPORT_SYMBOL(simple_dentry_operations);

struct dentry *simple_lookup(struct inode *dir, struct dentry *dentry, unsigned int flags) { return NULL; }
EXPORT_SYMBOL(simple_lookup);

int dcache_dir_open(struct inode *inode, struct file *file) { return 0; }
EXPORT_SYMBOL(dcache_dir_open);

int dcache_dir_close(struct inode *inode, struct file *file) { return 0; }
EXPORT_SYMBOL(dcache_dir_close);

loff_t dcache_dir_lseek(struct file *file, loff_t offset, int whence) { return 0; }
EXPORT_SYMBOL(dcache_dir_lseek);

int dcache_readdir(struct file *file, struct dir_context *ctx) { return 0; }
EXPORT_SYMBOL(dcache_readdir);

ssize_t generic_read_dir(struct file *filp, char __user *buf, size_t siz, loff_t *ppos) { return -EISDIR; }
EXPORT_SYMBOL(generic_read_dir);

const struct file_operations simple_dir_operations = { };
EXPORT_SYMBOL(simple_dir_operations);

const struct inode_operations simple_dir_inode_operations = { };
EXPORT_SYMBOL(simple_dir_inode_operations);

void simple_recursive_removal(struct dentry *dentry,
			      void (*callback)(struct dentry *)) { }
EXPORT_SYMBOL(simple_recursive_removal);

struct pseudo_fs_context *init_pseudo(struct fs_context *fc, unsigned long magic) { return ERR_PTR(-ENOMEM); }
EXPORT_SYMBOL(init_pseudo);

int simple_open(struct inode *inode, struct file *file) { return 0; }
EXPORT_SYMBOL(simple_open);

int simple_link(struct dentry *old_dentry, struct inode *dir, struct dentry *dentry) { return 0; }
EXPORT_SYMBOL(simple_link);

int simple_empty(struct dentry *dentry) { return 1; }
EXPORT_SYMBOL(simple_empty);

int simple_unlink(struct inode *dir, struct dentry *dentry) { return 0; }
EXPORT_SYMBOL(simple_unlink);

int simple_rmdir(struct inode *dir, struct dentry *dentry) { return 0; }
EXPORT_SYMBOL(simple_rmdir);

int simple_rename_exchange(struct inode *old_dir, struct dentry *old_dentry,
			    struct inode *new_dir, struct dentry *new_dentry) { return 0; }
EXPORT_SYMBOL_GPL(simple_rename_exchange);

int simple_rename(struct user_namespace *mnt_userns, struct inode *old_dir,
		  struct dentry *old_dentry, struct inode *new_dir,
		  struct dentry *new_dentry, unsigned int flags) { return 0; }
EXPORT_SYMBOL(simple_rename);

int simple_setattr(struct user_namespace *mnt_userns, struct dentry *dentry,
		   struct iattr *iattr) { return 0; }
EXPORT_SYMBOL(simple_setattr);

int simple_write_begin(struct file *file, struct address_space *mapping,
			loff_t pos, unsigned len,
			struct page **pagep, void **fsdata) { return 0; }
EXPORT_SYMBOL(simple_write_begin);

const struct address_space_operations ram_aops = { };
EXPORT_SYMBOL(ram_aops);

int simple_fill_super(struct super_block *s, unsigned long magic,
		      const struct tree_descr *files) { return 0; }
EXPORT_SYMBOL(simple_fill_super);

int simple_pin_fs(struct file_system_type *type, struct vfsmount **mount, int *count) { return 0; }
EXPORT_SYMBOL(simple_pin_fs);

void simple_release_fs(struct vfsmount **mount, int *count) { }
EXPORT_SYMBOL(simple_release_fs);

ssize_t simple_read_from_buffer(void __user *to, size_t count, loff_t *ppos,
				const void *from, size_t available) { return 0; }
EXPORT_SYMBOL(simple_read_from_buffer);

ssize_t simple_write_to_buffer(void *to, size_t available, loff_t *ppos,
			       const void __user *from, size_t count) { return 0; }
EXPORT_SYMBOL(simple_write_to_buffer);

ssize_t memory_read_from_buffer(void *to, size_t count, loff_t *ppos,
				const void *from, size_t available) { return 0; }
EXPORT_SYMBOL(memory_read_from_buffer);

void simple_transaction_set(struct file *file, size_t n) { }
EXPORT_SYMBOL(simple_transaction_set);

char *simple_transaction_get(struct file *file, const char __user *buf, size_t size) { return ERR_PTR(-ENOMEM); }
EXPORT_SYMBOL(simple_transaction_get);

ssize_t simple_transaction_read(struct file *file, char __user *buf, size_t size, loff_t *pos) { return 0; }
EXPORT_SYMBOL(simple_transaction_read);

int simple_transaction_release(struct inode *inode, struct file *file) { return 0; }
EXPORT_SYMBOL(simple_transaction_release);

int simple_attr_open(struct inode *inode, struct file *file,
		     int (*get)(void *, u64 *), int (*set)(void *, u64),
		     const char *fmt) { return 0; }
EXPORT_SYMBOL_GPL(simple_attr_open);

int simple_attr_release(struct inode *inode, struct file *file) { return 0; }
EXPORT_SYMBOL_GPL(simple_attr_release);

ssize_t simple_attr_read(struct file *file, char __user *buf,
			 size_t len, loff_t *ppos) { return 0; }
EXPORT_SYMBOL_GPL(simple_attr_read);

ssize_t simple_attr_write(struct file *file, const char __user *buf,
			  size_t len, loff_t *ppos) { return len; }
EXPORT_SYMBOL_GPL(simple_attr_write);

struct dentry *generic_fh_to_dentry(struct super_block *sb, struct fid *fid,
				    int fh_len, int fh_type,
				    struct inode *(*get_inode)(struct super_block *sb, u64 ino, u32 gen)) { return NULL; }
EXPORT_SYMBOL_GPL(generic_fh_to_dentry);

struct dentry *generic_fh_to_parent(struct super_block *sb, struct fid *fid,
				    int fh_len, int fh_type,
				    struct inode *(*get_inode)(struct super_block *sb, u64 ino, u32 gen)) { return NULL; }
EXPORT_SYMBOL_GPL(generic_fh_to_parent);

int generic_file_fsync(struct file *file, loff_t start, loff_t end, int datasync) { return 0; }
EXPORT_SYMBOL(generic_file_fsync);

int generic_check_addressable(unsigned blocksize_bits, u64 num_blocks) { return 0; }
EXPORT_SYMBOL(generic_check_addressable);

int noop_fsync(struct file *file, loff_t start, loff_t end, int datasync) { return 0; }
EXPORT_SYMBOL(noop_fsync);

ssize_t noop_direct_IO(struct kiocb *iocb, struct iov_iter *iter) { return -EINVAL; }
EXPORT_SYMBOL_GPL(noop_direct_IO);

void noop_invalidatepage(struct page *page, unsigned int offset, unsigned int length) { }
EXPORT_SYMBOL(noop_invalidatepage);

int noop_set_page_dirty(struct page *page) { return 0; }
EXPORT_SYMBOL(noop_set_page_dirty);

void kfree_link(void *p) { kfree(p); }
EXPORT_SYMBOL(kfree_link);

struct inode *alloc_anon_inode(struct super_block *s) { return ERR_PTR(-ENOMEM); }
EXPORT_SYMBOL(alloc_anon_inode);

int simple_nosetlease(struct file *filp, long arg, struct file_lock **flp, void **priv) { return -EINVAL; }
EXPORT_SYMBOL(simple_nosetlease);

ssize_t generic_copy_file_range(struct file *file_in, loff_t pos_in,
				struct file *file_out, loff_t pos_out,
				size_t len, unsigned int flags) { return -EINVAL; }
EXPORT_SYMBOL(generic_copy_file_range);

bool is_empty_dir_inode(struct inode *inode) { return true; }
