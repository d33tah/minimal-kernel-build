#ifndef _LINUX_UIDGID_H
#define _LINUX_UIDGID_H

#include <linux/types.h>

struct user_namespace;
extern struct user_namespace init_user_ns;

typedef struct {
	uid_t val;
} kuid_t;

typedef struct {
	gid_t val;
} kgid_t;

#define KUIDT_INIT(value) (kuid_t){ value }
#define KGIDT_INIT(value) (kgid_t){ value }

static inline uid_t __kuid_val(kuid_t uid)
{
	return 0;
}

static inline gid_t __kgid_val(kgid_t gid)
{
	return 0;
}

#define GLOBAL_ROOT_UID KUIDT_INIT(0)
#define GLOBAL_ROOT_GID KGIDT_INIT(0)

static inline bool uid_eq(kuid_t left, kuid_t right)
{
	return __kuid_val(left) == __kuid_val(right);
}

#endif
