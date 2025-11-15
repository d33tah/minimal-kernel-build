 
 

#ifndef _LINUX_EVM_H
#define _LINUX_EVM_H

#include <linux/integrity.h>
#include <linux/xattr.h>

struct integrity_iint_cache;


static inline int evm_set_key(void *key, size_t keylen)
{
	return -EOPNOTSUPP;
}


static inline int evm_inode_setattr(struct dentry *dentry, struct iattr *attr)
{
	return 0;
}

static inline void evm_inode_post_setattr(struct dentry *dentry, int ia_valid)
{
	return;
}

static inline int evm_inode_setxattr(struct user_namespace *mnt_userns,
				     struct dentry *dentry, const char *name,
				     const void *value, size_t size)
{
	return 0;
}

static inline void evm_inode_post_setxattr(struct dentry *dentry,
					   const char *xattr_name,
					   const void *xattr_value,
					   size_t xattr_value_len)
{
	return;
}

static inline int evm_inode_removexattr(struct user_namespace *mnt_userns,
					struct dentry *dentry,
					const char *xattr_name)
{
	return 0;
}

static inline void evm_inode_post_removexattr(struct dentry *dentry,
					      const char *xattr_name)
{
	return;
}

static inline int evm_inode_init_security(struct inode *inode,
					  const struct xattr *xattr_array,
					  struct xattr *evm)
{
	return 0;
}

static inline bool evm_revalidate_status(const char *xattr_name)
{
	return false;
}

static inline int evm_protected_xattr_if_enabled(const char *req_xattr_name)
{
	return false;
}

static inline int evm_read_protected_xattrs(struct dentry *dentry, u8 *buffer,
					    int buffer_size, char type,
					    bool canonical_fmt)
{
	return -EOPNOTSUPP;
}

#endif  
