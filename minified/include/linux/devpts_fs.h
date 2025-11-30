 
 

#ifndef _LINUX_DEVPTS_FS_H
#define _LINUX_DEVPTS_FS_H

#include <linux/errno.h>

static inline int
ptm_open_peer(struct file *master, struct tty_struct *tty, int flags)
{
	return -EIO;
}


#endif  
