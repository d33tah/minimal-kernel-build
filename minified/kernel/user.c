// SPDX-License-Identifier: GPL-2.0-only
/*
 * The "user cache".
 *
 * (C) Copyright 1991-2000 Linus Torvalds
 *
 * We have a per-user structure to keep track of how many
 * processes, files etc the user has claimed, in order to be
 * able to have per-user limits for system resources. 
 */

#include <linux/sched/user.h>
#include <linux/user_namespace.h>

struct user_namespace init_user_ns;

/* root_user.__count is 1, for init task cred */
struct user_struct root_user = {
	.__count = REFCOUNT_INIT(1),
};
