/* Minimal cgroup.h - stubs for !CONFIG_CGROUPS */
#ifndef _LINUX_CGROUP_H
#define _LINUX_CGROUP_H
#include <linux/fs.h>
#include <linux/jump_label.h>
#include <linux/user_namespace.h>
struct cgroup_namespace { struct ns_common ns; struct user_namespace *user_ns; struct ucounts *ucounts; struct css_set *root_cset; };
#endif
