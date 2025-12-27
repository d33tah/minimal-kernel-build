 
#include <linux/kernel.h>
#include <linux/blkdev.h>
#include <linux/init.h>
#include <linux/syscalls.h>
#include <linux/unistd.h>
#include <linux/slab.h>
#include <linux/mount.h>
#include <linux/major.h>
#include <linux/root_dev.h>
#include <linux/init_syscalls.h>

void  mount_block_root(char *name, int flags);
void  mount_root(void);
extern int root_mountflags;

/* Removed uncalled: rd_load_disk, rd_load_image, create_dev */



bool __init initrd_load(void);

