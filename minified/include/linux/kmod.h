 
#ifndef __LINUX_KMOD_H__
#define __LINUX_KMOD_H__

 

#include <linux/umh.h>
#include <linux/gfp.h>
#include <linux/stddef.h>
#include <linux/errno.h>
#include <linux/compiler.h>
#include <linux/workqueue.h>
#include <linux/sysctl.h>

#define KMOD_PATH_LEN 256

static inline int request_module(const char *name, ...) { return -ENOSYS; }
static inline int request_module_nowait(const char *name, ...) { return -ENOSYS; }
#define try_then_request_module(x, mod...) (x)

#endif  
