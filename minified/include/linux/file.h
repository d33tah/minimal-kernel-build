/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Wrapper functions for accessing the file_struct fd array.
 */

#ifndef __LINUX_FILE_H
#define __LINUX_FILE_H

struct file;

extern void fput(struct file *);
extern struct file *fget_light(unsigned int fd);

extern int get_unused_fd_flags(unsigned flags);

extern void fd_install(unsigned int fd, struct file *file);

#endif /* __LINUX_FILE_H */
