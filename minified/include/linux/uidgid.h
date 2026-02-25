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

#define GLOBAL_ROOT_UID KUIDT_INIT(0)
#define GLOBAL_ROOT_GID KGIDT_INIT(0)

#endif
