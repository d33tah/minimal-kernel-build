
#ifndef __LINUX_FILE_H
#define __LINUX_FILE_H

#include <linux/compiler.h>
#include <linux/types.h>
#include <asm/posix_types.h>

struct file;

extern void fput(struct file *);

struct fd {
	struct file *file;
	unsigned int flags;
};
#define FDPUT_FPUT       1
#define FDPUT_POS_UNLOCK 2

extern void __f_unlock_pos(struct file *);

static inline void fdput(struct fd fd)
{
	if (fd.flags & FDPUT_POS_UNLOCK)
		__f_unlock_pos(fd.file);
	fput(fd.file);
}
extern void fput(struct file *file);

extern unsigned long __fdget_raw(unsigned int fd);
extern unsigned long __fdget_pos(unsigned int fd);

static inline struct fd __to_fd(unsigned long v)
{
	return (struct fd){(struct file *)(v & ~3),v & 3};
}

static inline struct fd fdget_raw(unsigned int fd)
{
	return __to_fd(__fdget_raw(fd));
}

static inline struct fd fdget_pos(int fd)
{
	return __to_fd(__fdget_pos(fd));
}

/* fdput_pos inlined at fs/read_write.c - single caller */

extern int get_unused_fd_flags(unsigned flags);

extern void fd_install(unsigned int fd, struct file *file);

#endif
