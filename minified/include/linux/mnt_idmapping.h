#ifndef _LINUX_MNT_IDMAPPING_H
#define _LINUX_MNT_IDMAPPING_H

#include <linux/types.h>
#include <linux/uidgid.h>

struct user_namespace;
extern struct user_namespace init_user_ns;

static inline bool initial_idmapping(const struct user_namespace *ns)
{
	return ns == &init_user_ns;
}

static inline bool no_idmapping(const struct user_namespace *mnt_userns,
				const struct user_namespace *fs_userns)
{
	return initial_idmapping(mnt_userns) || mnt_userns == fs_userns;
}

static inline kuid_t mapped_kuid_fs(struct user_namespace *mnt_userns,
				    struct user_namespace *fs_userns,
				    kuid_t kuid)
{
	uid_t uid;

	if (no_idmapping(mnt_userns, fs_userns))
		return kuid;
	if (initial_idmapping(fs_userns))
		uid = __kuid_val(kuid);
	else
		uid = from_kuid(fs_userns, kuid);
	if (uid == (uid_t)-1)
		return INVALID_UID;
	return make_kuid(mnt_userns, uid);
}

static inline kgid_t mapped_kgid_fs(struct user_namespace *mnt_userns,
				    struct user_namespace *fs_userns,
				    kgid_t kgid)
{
	gid_t gid;

	if (no_idmapping(mnt_userns, fs_userns))
		return kgid;
	if (initial_idmapping(fs_userns))
		gid = __kgid_val(kgid);
	else
		gid = from_kgid(fs_userns, kgid);
	if (gid == (gid_t)-1)
		return INVALID_GID;
	return make_kgid(mnt_userns, gid);
}

static inline kuid_t mapped_kuid_user(struct user_namespace *mnt_userns,
				      struct user_namespace *fs_userns,
				      kuid_t kuid)
{
	uid_t uid;

	if (no_idmapping(mnt_userns, fs_userns))
		return kuid;
	uid = from_kuid(mnt_userns, kuid);
	if (uid == (uid_t)-1)
		return INVALID_UID;
	if (initial_idmapping(fs_userns))
		return KUIDT_INIT(uid);
	return make_kuid(fs_userns, uid);
}

static inline kgid_t mapped_kgid_user(struct user_namespace *mnt_userns,
				      struct user_namespace *fs_userns,
				      kgid_t kgid)
{
	gid_t gid;

	if (no_idmapping(mnt_userns, fs_userns))
		return kgid;
	gid = from_kgid(mnt_userns, kgid);
	if (gid == (gid_t)-1)
		return INVALID_GID;
	if (initial_idmapping(fs_userns))
		return KGIDT_INIT(gid);
	return make_kgid(fs_userns, gid);
}

static inline kuid_t mapped_fsuid(struct user_namespace *mnt_userns,
				  struct user_namespace *fs_userns)
{
	return mapped_kuid_user(mnt_userns, fs_userns, current_fsuid());
}

static inline kgid_t mapped_fsgid(struct user_namespace *mnt_userns,
				  struct user_namespace *fs_userns)
{
	return mapped_kgid_user(mnt_userns, fs_userns, current_fsgid());
}

#endif  
