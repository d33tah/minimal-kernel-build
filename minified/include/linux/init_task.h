#ifndef _LINUX__INIT_TASK_H
#define _LINUX__INIT_TASK_H

#include <linux/lockdep.h>
#include <linux/pid_namespace.h>
#include <linux/user_namespace.h>

#include <linux/mm_types.h>


extern struct files_struct init_files;
extern struct fs_struct init_fs;
extern struct nsproxy init_nsproxy;
extern struct cred init_cred;

#define INIT_TASK_COMM "swapper"

#endif
