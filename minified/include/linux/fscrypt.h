/* SPDX-License-Identifier: GPL-2.0 */
/*
 * fscrypt.h: declarations for per-file encryption
 *
 * Filesystems that implement per-file encryption must include this header
 * file.
 *
 * Copyright (C) 2015, Google, Inc.
 *
 * Written by Michael Halcrow, 2015.
 * Modified by Jaegeuk Kim, 2015.
 */
#ifndef _LINUX_FSCRYPT_H
#define _LINUX_FSCRYPT_H

#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/slab.h>


/*
 * The lengths of all file contents blocks must be divisible by this value.
 * This is needed to ensure that all contents encryption modes will work, as
 * some of the supported modes don't support arbitrarily byte-aligned messages.
 *
 * Since the needed alignment is 16 bytes, most filesystems will meet this
 * requirement naturally, as typical block sizes are powers of 2.  However, if a
 * filesystem can generate arbitrarily byte-aligned block lengths (e.g., via
 * compression), then it will need to pad to this alignment before encryption.
 */
#define FSCRYPT_CONTENTS_ALIGNMENT 16

union fscrypt_policy;
struct fscrypt_info;
struct fs_parameter;
struct seq_file;

struct fscrypt_str {
	unsigned char *name;
	u32 len;
};

struct fscrypt_name {
	const struct qstr *usr_fname;
	struct fscrypt_str disk_name;
	u32 hash;
	u32 minor_hash;
	struct fscrypt_str crypto_buf;
	bool is_nokey_name;
};

#define FSTR_INIT(n, l)		{ .name = n, .len = l }
#define FSTR_TO_QSTR(f)		QSTR_INIT((f)->name, (f)->len)
#define fname_name(p)		((p)->disk_name.name)
#define fname_len(p)		((p)->disk_name.len)

/* Maximum value for the third parameter of fscrypt_operations.set_context(). */
#define FSCRYPT_SET_CONTEXT_MAX_SIZE	40


static inline struct fscrypt_info *fscrypt_get_info(const struct inode *inode)
{
	return NULL;
}

static inline bool fscrypt_needs_contents_encryption(const struct inode *inode)
{
	return false;
}

static inline void fscrypt_handle_d_move(struct dentry *dentry)
{
}

static inline bool fscrypt_is_nokey_name(const struct dentry *dentry)
{
	return false;
}

/* crypto.c */
static inline void fscrypt_enqueue_decrypt_work(struct work_struct *work)
{
}

static inline struct page *fscrypt_encrypt_pagecache_blocks(struct page *page,
							    unsigned int len,
							    unsigned int offs,
							    gfp_t gfp_flags)
{
	return ERR_PTR(-EOPNOTSUPP);
}

static inline int fscrypt_encrypt_block_inplace(const struct inode *inode,
						struct page *page,
						unsigned int len,
						unsigned int offs, u64 lblk_num,
						gfp_t gfp_flags)
{
	return -EOPNOTSUPP;
}

static inline int fscrypt_decrypt_pagecache_blocks(struct page *page,
						   unsigned int len,
						   unsigned int offs)
{
	return -EOPNOTSUPP;
}

static inline int fscrypt_decrypt_block_inplace(const struct inode *inode,
						struct page *page,
						unsigned int len,
						unsigned int offs, u64 lblk_num)
{
	return -EOPNOTSUPP;
}

static inline bool fscrypt_is_bounce_page(struct page *page)
{
	return false;
}

static inline struct page *fscrypt_pagecache_page(struct page *bounce_page)
{
	WARN_ON_ONCE(1);
	return ERR_PTR(-EINVAL);
}

static inline void fscrypt_free_bounce_page(struct page *bounce_page)
{
}

/* policy.c */
static inline int fscrypt_ioctl_set_policy(struct file *filp,
					   const void __user *arg)
{
	return -EOPNOTSUPP;
}

static inline int fscrypt_ioctl_get_policy(struct file *filp, void __user *arg)
{
	return -EOPNOTSUPP;
}

static inline int fscrypt_ioctl_get_policy_ex(struct file *filp,
					      void __user *arg)
{
	return -EOPNOTSUPP;
}

static inline int fscrypt_ioctl_get_nonce(struct file *filp, void __user *arg)
{
	return -EOPNOTSUPP;
}

static inline int fscrypt_has_permitted_context(struct inode *parent,
						struct inode *child)
{
	return 0;
}

static inline int fscrypt_set_context(struct inode *inode, void *fs_data)
{
	return -EOPNOTSUPP;
}

struct fscrypt_dummy_policy {
};

static inline int
fscrypt_parse_test_dummy_encryption(const struct fs_parameter *param,
				    struct fscrypt_dummy_policy *dummy_policy)
{
	return -EINVAL;
}

static inline bool
fscrypt_dummy_policies_equal(const struct fscrypt_dummy_policy *p1,
			     const struct fscrypt_dummy_policy *p2)
{
	return true;
}

static inline void fscrypt_show_test_dummy_encryption(struct seq_file *seq,
						      char sep,
						      struct super_block *sb)
{
}

static inline bool
fscrypt_is_dummy_policy_set(const struct fscrypt_dummy_policy *dummy_policy)
{
	return false;
}

static inline void
fscrypt_free_dummy_policy(struct fscrypt_dummy_policy *dummy_policy)
{
}

/* keyring.c */
static inline void fscrypt_sb_free(struct super_block *sb)
{
}

static inline int fscrypt_ioctl_add_key(struct file *filp, void __user *arg)
{
	return -EOPNOTSUPP;
}

static inline int
fscrypt_add_test_dummy_key(struct super_block *sb,
			   const struct fscrypt_dummy_policy *dummy_policy)
{
	return 0;
}

static inline int fscrypt_ioctl_remove_key(struct file *filp, void __user *arg)
{
	return -EOPNOTSUPP;
}

static inline int fscrypt_ioctl_remove_key_all_users(struct file *filp,
						     void __user *arg)
{
	return -EOPNOTSUPP;
}

static inline int fscrypt_ioctl_get_key_status(struct file *filp,
					       void __user *arg)
{
	return -EOPNOTSUPP;
}

/* keysetup.c */

static inline int fscrypt_prepare_new_inode(struct inode *dir,
					    struct inode *inode,
					    bool *encrypt_ret)
{
	if (IS_ENCRYPTED(dir))
		return -EOPNOTSUPP;
	return 0;
}

static inline void fscrypt_put_encryption_info(struct inode *inode)
{
	return;
}

static inline void fscrypt_free_inode(struct inode *inode)
{
}

static inline int fscrypt_drop_inode(struct inode *inode)
{
	return 0;
}

 /* fname.c */
static inline int fscrypt_setup_filename(struct inode *dir,
					 const struct qstr *iname,
					 int lookup, struct fscrypt_name *fname)
{
	if (IS_ENCRYPTED(dir))
		return -EOPNOTSUPP;

	memset(fname, 0, sizeof(*fname));
	fname->usr_fname = iname;
	fname->disk_name.name = (unsigned char *)iname->name;
	fname->disk_name.len = iname->len;
	return 0;
}

static inline void fscrypt_free_filename(struct fscrypt_name *fname)
{
	return;
}

static inline int fscrypt_fname_alloc_buffer(u32 max_encrypted_len,
					     struct fscrypt_str *crypto_str)
{
	return -EOPNOTSUPP;
}

static inline void fscrypt_fname_free_buffer(struct fscrypt_str *crypto_str)
{
	return;
}

static inline int fscrypt_fname_disk_to_usr(const struct inode *inode,
					    u32 hash, u32 minor_hash,
					    const struct fscrypt_str *iname,
					    struct fscrypt_str *oname)
{
	return -EOPNOTSUPP;
}

static inline bool fscrypt_match_name(const struct fscrypt_name *fname,
				      const u8 *de_name, u32 de_name_len)
{
	/* Encryption support disabled; use standard comparison */
	if (de_name_len != fname->disk_name.len)
		return false;
	return !memcmp(de_name, fname->disk_name.name, fname->disk_name.len);
}

static inline u64 fscrypt_fname_siphash(const struct inode *dir,
					const struct qstr *name)
{
	WARN_ON_ONCE(1);
	return 0;
}

static inline int fscrypt_d_revalidate(struct dentry *dentry,
				       unsigned int flags)
{
	return 1;
}

/* bio.c */
static inline void fscrypt_decrypt_bio(struct bio *bio)
{
}

static inline int fscrypt_zeroout_range(const struct inode *inode, pgoff_t lblk,
					sector_t pblk, unsigned int len)
{
	return -EOPNOTSUPP;
}

/* hooks.c */

static inline int fscrypt_file_open(struct inode *inode, struct file *filp)
{
	if (IS_ENCRYPTED(inode))
		return -EOPNOTSUPP;
	return 0;
}

static inline int __fscrypt_prepare_link(struct inode *inode, struct inode *dir,
					 struct dentry *dentry)
{
	return -EOPNOTSUPP;
}

static inline int __fscrypt_prepare_rename(struct inode *old_dir,
					   struct dentry *old_dentry,
					   struct inode *new_dir,
					   struct dentry *new_dentry,
					   unsigned int flags)
{
	return -EOPNOTSUPP;
}

static inline int __fscrypt_prepare_lookup(struct inode *dir,
					   struct dentry *dentry,
					   struct fscrypt_name *fname)
{
	return -EOPNOTSUPP;
}

static inline int __fscrypt_prepare_readdir(struct inode *dir)
{
	return -EOPNOTSUPP;
}

static inline int __fscrypt_prepare_setattr(struct dentry *dentry,
					    struct iattr *attr)
{
	return -EOPNOTSUPP;
}

static inline int fscrypt_prepare_setflags(struct inode *inode,
					   unsigned int oldflags,
					   unsigned int flags)
{
	return 0;
}

static inline int fscrypt_prepare_symlink(struct inode *dir,
					  const char *target,
					  unsigned int len,
					  unsigned int max_len,
					  struct fscrypt_str *disk_link)
{
	if (IS_ENCRYPTED(dir))
		return -EOPNOTSUPP;
	disk_link->name = (unsigned char *)target;
	disk_link->len = len + 1;
	if (disk_link->len > max_len)
		return -ENAMETOOLONG;
	return 0;
}

static inline int __fscrypt_encrypt_symlink(struct inode *inode,
					    const char *target,
					    unsigned int len,
					    struct fscrypt_str *disk_link)
{
	return -EOPNOTSUPP;
}

static inline const char *fscrypt_get_symlink(struct inode *inode,
					      const void *caddr,
					      unsigned int max_size,
					      struct delayed_call *done)
{
	return ERR_PTR(-EOPNOTSUPP);
}

static inline int fscrypt_symlink_getattr(const struct path *path,
					  struct kstat *stat)
{
	return -EOPNOTSUPP;
}

static inline void fscrypt_set_ops(struct super_block *sb,
				   const struct fscrypt_operations *s_cop)
{
}


/* inline_crypt.c */

static inline bool __fscrypt_inode_uses_inline_crypto(const struct inode *inode)
{
	return false;
}

static inline void fscrypt_set_bio_crypt_ctx(struct bio *bio,
					     const struct inode *inode,
					     u64 first_lblk, gfp_t gfp_mask) { }

static inline void fscrypt_set_bio_crypt_ctx_bh(
					 struct bio *bio,
					 const struct buffer_head *first_bh,
					 gfp_t gfp_mask) { }

static inline bool fscrypt_mergeable_bio(struct bio *bio,
					 const struct inode *inode,
					 u64 next_lblk)
{
	return true;
}

static inline bool fscrypt_mergeable_bio_bh(struct bio *bio,
					    const struct buffer_head *next_bh)
{
	return true;
}

static inline bool fscrypt_dio_supported(struct kiocb *iocb,
					 struct iov_iter *iter)
{
	const struct inode *inode = file_inode(iocb->ki_filp);

	return !fscrypt_needs_contents_encryption(inode);
}

static inline u64 fscrypt_limit_io_blocks(const struct inode *inode, u64 lblk,
					  u64 nr_blocks)
{
	return nr_blocks;
}

/**
 * fscrypt_inode_uses_inline_crypto() - test whether an inode uses inline
 *					encryption
 * @inode: an inode. If encrypted, its key must be set up.
 *
 * Return: true if the inode requires file contents encryption and if the
 *	   encryption should be done in the block layer via blk-crypto rather
 *	   than in the filesystem layer.
 */
static inline bool fscrypt_inode_uses_inline_crypto(const struct inode *inode)
{
	return fscrypt_needs_contents_encryption(inode) &&
	       __fscrypt_inode_uses_inline_crypto(inode);
}

/**
 * fscrypt_inode_uses_fs_layer_crypto() - test whether an inode uses fs-layer
 *					  encryption
 * @inode: an inode. If encrypted, its key must be set up.
 *
 * Return: true if the inode requires file contents encryption and if the
 *	   encryption should be done in the filesystem layer rather than in the
 *	   block layer via blk-crypto.
 */
static inline bool fscrypt_inode_uses_fs_layer_crypto(const struct inode *inode)
{
	return fscrypt_needs_contents_encryption(inode) &&
	       !__fscrypt_inode_uses_inline_crypto(inode);
}

/**
 * fscrypt_has_encryption_key() - check whether an inode has had its key set up
 * @inode: the inode to check
 *
 * Return: %true if the inode has had its encryption key set up, else %false.
 *
 * Usually this should be preceded by fscrypt_get_encryption_info() to try to
 * set up the key first.
 */
static inline bool fscrypt_has_encryption_key(const struct inode *inode)
{
	return fscrypt_get_info(inode) != NULL;
}

/**
 * fscrypt_prepare_link() - prepare to link an inode into a possibly-encrypted
 *			    directory
 * @old_dentry: an existing dentry for the inode being linked
 * @dir: the target directory
 * @dentry: negative dentry for the target filename
 *
 * A new link can only be added to an encrypted directory if the directory's
 * encryption key is available --- since otherwise we'd have no way to encrypt
 * the filename.
 *
 * We also verify that the link will not violate the constraint that all files
 * in an encrypted directory tree use the same encryption policy.
 *
 * Return: 0 on success, -ENOKEY if the directory's encryption key is missing,
 * -EXDEV if the link would result in an inconsistent encryption policy, or
 * another -errno code.
 */
static inline int fscrypt_prepare_link(struct dentry *old_dentry,
				       struct inode *dir,
				       struct dentry *dentry)
{
	if (IS_ENCRYPTED(dir))
		return __fscrypt_prepare_link(d_inode(old_dentry), dir, dentry);
	return 0;
}

/**
 * fscrypt_prepare_rename() - prepare for a rename between possibly-encrypted
 *			      directories
 * @old_dir: source directory
 * @old_dentry: dentry for source file
 * @new_dir: target directory
 * @new_dentry: dentry for target location (may be negative unless exchanging)
 * @flags: rename flags (we care at least about %RENAME_EXCHANGE)
 *
 * Prepare for ->rename() where the source and/or target directories may be
 * encrypted.  A new link can only be added to an encrypted directory if the
 * directory's encryption key is available --- since otherwise we'd have no way
 * to encrypt the filename.  A rename to an existing name, on the other hand,
 * *is* cryptographically possible without the key.  However, we take the more
 * conservative approach and just forbid all no-key renames.
 *
 * We also verify that the rename will not violate the constraint that all files
 * in an encrypted directory tree use the same encryption policy.
 *
 * Return: 0 on success, -ENOKEY if an encryption key is missing, -EXDEV if the
 * rename would cause inconsistent encryption policies, or another -errno code.
 */
static inline int fscrypt_prepare_rename(struct inode *old_dir,
					 struct dentry *old_dentry,
					 struct inode *new_dir,
					 struct dentry *new_dentry,
					 unsigned int flags)
{
	if (IS_ENCRYPTED(old_dir) || IS_ENCRYPTED(new_dir))
		return __fscrypt_prepare_rename(old_dir, old_dentry,
						new_dir, new_dentry, flags);
	return 0;
}

/**
 * fscrypt_prepare_lookup() - prepare to lookup a name in a possibly-encrypted
 *			      directory
 * @dir: directory being searched
 * @dentry: filename being looked up
 * @fname: (output) the name to use to search the on-disk directory
 *
 * Prepare for ->lookup() in a directory which may be encrypted by determining
 * the name that will actually be used to search the directory on-disk.  If the
 * directory's encryption policy is supported by this kernel and its encryption
 * key is available, then the lookup is assumed to be by plaintext name;
 * otherwise, it is assumed to be by no-key name.
 *
 * This will set DCACHE_NOKEY_NAME on the dentry if the lookup is by no-key
 * name.  In this case the filesystem must assign the dentry a dentry_operations
 * which contains fscrypt_d_revalidate (or contains a d_revalidate method that
 * calls fscrypt_d_revalidate), so that the dentry will be invalidated if the
 * directory's encryption key is later added.
 *
 * Return: 0 on success; -ENOENT if the directory's key is unavailable but the
 * filename isn't a valid no-key name, so a negative dentry should be created;
 * or another -errno code.
 */
static inline int fscrypt_prepare_lookup(struct inode *dir,
					 struct dentry *dentry,
					 struct fscrypt_name *fname)
{
	if (IS_ENCRYPTED(dir))
		return __fscrypt_prepare_lookup(dir, dentry, fname);

	memset(fname, 0, sizeof(*fname));
	fname->usr_fname = &dentry->d_name;
	fname->disk_name.name = (unsigned char *)dentry->d_name.name;
	fname->disk_name.len = dentry->d_name.len;
	return 0;
}

/**
 * fscrypt_prepare_readdir() - prepare to read a possibly-encrypted directory
 * @dir: the directory inode
 *
 * If the directory is encrypted and it doesn't already have its encryption key
 * set up, try to set it up so that the filenames will be listed in plaintext
 * form rather than in no-key form.
 *
 * Return: 0 on success; -errno on error.  Note that the encryption key being
 *	   unavailable is not considered an error.  It is also not an error if
 *	   the encryption policy is unsupported by this kernel; that is treated
 *	   like the key being unavailable, so that files can still be deleted.
 */
static inline int fscrypt_prepare_readdir(struct inode *dir)
{
	if (IS_ENCRYPTED(dir))
		return __fscrypt_prepare_readdir(dir);
	return 0;
}

/**
 * fscrypt_prepare_setattr() - prepare to change a possibly-encrypted inode's
 *			       attributes
 * @dentry: dentry through which the inode is being changed
 * @attr: attributes to change
 *
 * Prepare for ->setattr() on a possibly-encrypted inode.  On an encrypted file,
 * most attribute changes are allowed even without the encryption key.  However,
 * without the encryption key we do have to forbid truncates.  This is needed
 * because the size being truncated to may not be a multiple of the filesystem
 * block size, and in that case we'd have to decrypt the final block, zero the
 * portion past i_size, and re-encrypt it.  (We *could* allow truncating to a
 * filesystem block boundary, but it's simpler to just forbid all truncates ---
 * and we already forbid all other contents modifications without the key.)
 *
 * Return: 0 on success, -ENOKEY if the key is missing, or another -errno code
 * if a problem occurred while setting up the encryption key.
 */
static inline int fscrypt_prepare_setattr(struct dentry *dentry,
					  struct iattr *attr)
{
	if (IS_ENCRYPTED(d_inode(dentry)))
		return __fscrypt_prepare_setattr(dentry, attr);
	return 0;
}

/**
 * fscrypt_encrypt_symlink() - encrypt the symlink target if needed
 * @inode: symlink inode
 * @target: plaintext symlink target
 * @len: length of @target excluding null terminator
 * @disk_link: (in/out) the on-disk symlink target being prepared
 *
 * If the symlink target needs to be encrypted, then this function encrypts it
 * into @disk_link->name.  fscrypt_prepare_symlink() must have been called
 * previously to compute @disk_link->len.  If the filesystem did not allocate a
 * buffer for @disk_link->name after calling fscrypt_prepare_link(), then one
 * will be kmalloc()'ed and the filesystem will be responsible for freeing it.
 *
 * Return: 0 on success, -errno on failure
 */
static inline int fscrypt_encrypt_symlink(struct inode *inode,
					  const char *target,
					  unsigned int len,
					  struct fscrypt_str *disk_link)
{
	if (IS_ENCRYPTED(inode))
		return __fscrypt_encrypt_symlink(inode, target, len, disk_link);
	return 0;
}

/* If *pagep is a bounce page, free it and set *pagep to the pagecache page */
static inline void fscrypt_finalize_bounce_page(struct page **pagep)
{
	struct page *page = *pagep;

	if (fscrypt_is_bounce_page(page)) {
		*pagep = fscrypt_pagecache_page(page);
		fscrypt_free_bounce_page(page);
	}
}

#endif	/* _LINUX_FSCRYPT_H */
