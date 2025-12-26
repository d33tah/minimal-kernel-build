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

#define fname_name(p)		((p)->disk_name.name)
#define fname_len(p)		((p)->disk_name.len)

static inline void fscrypt_sb_free(struct super_block *sb) { }

#endif
