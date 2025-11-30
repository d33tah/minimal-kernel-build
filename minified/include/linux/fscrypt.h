#ifndef _LINUX_FSCRYPT_H
#define _LINUX_FSCRYPT_H

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

struct fscrypt_dummy_policy { };

#define fname_name(p)		((p)->disk_name.name)
#define fname_len(p)		((p)->disk_name.len)

static inline void fscrypt_handle_d_move(struct dentry *dentry) { }
static inline void fscrypt_sb_free(struct super_block *sb) { }
static inline bool fscrypt_needs_contents_encryption(const struct inode *inode) { return false; }
static inline int fscrypt_setup_filename(struct inode *dir, const struct qstr *iname, int lookup, struct fscrypt_name *fname)
{
	memset(fname, 0, sizeof(*fname));
	fname->usr_fname = iname;
	fname->disk_name.name = (unsigned char *)iname->name;
	fname->disk_name.len = iname->len;
	return 0;
}
static inline void fscrypt_free_filename(struct fscrypt_name *fname) { }
static inline bool fscrypt_match_name(const struct fscrypt_name *fname, const u8 *de_name, u32 de_name_len)
{
	if (de_name_len != fname->disk_name.len)
		return false;
	return !memcmp(de_name, fname->disk_name.name, fname->disk_name.len);
}
static inline int fscrypt_prepare_lookup(struct inode *dir, struct dentry *dentry, struct fscrypt_name *fname)
{
	memset(fname, 0, sizeof(*fname));
	fname->usr_fname = &dentry->d_name;
	fname->disk_name.name = (unsigned char *)dentry->d_name.name;
	fname->disk_name.len = dentry->d_name.len;
	return 0;
}
static inline int fscrypt_prepare_symlink(struct inode *dir, const char *target, unsigned int len, unsigned int max_len, struct fscrypt_str *disk_link)
{
	disk_link->name = (unsigned char *)target;
	disk_link->len = len + 1;
	if (disk_link->len > max_len)
		return -ENAMETOOLONG;
	return 0;
}

#endif
