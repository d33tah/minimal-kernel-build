#ifndef __LINUX_UMH_H__
#define __LINUX_UMH_H__
#include <linux/gfp.h>
#include <linux/stddef.h>
#include <linux/errno.h>
#include <linux/compiler.h>
#include <linux/workqueue.h>
#include <linux/sysctl.h>
struct cred;
struct file;
#define UMH_NO_WAIT	0
#define UMH_WAIT_EXEC	1
#define UMH_WAIT_PROC	2
#define UMH_KILLABLE	4
struct subprocess_info {
	struct work_struct work; struct completion *complete; const char *path;
	char **argv; char **envp; int wait; int retval;
	int (*init)(struct subprocess_info *info, struct cred *new);
	void (*cleanup)(struct subprocess_info *info); void *data;
} __randomize_layout;
enum umh_disable_depth { UMH_ENABLED = 0, UMH_DISABLED, };
/* usermodehelper functions removed - never called in minimal kernel */
#endif
