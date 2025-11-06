// SPDX-License-Identifier: GPL-2.0
/* Stubbed file.c */
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/fdtable.h>
#include <linux/export.h>
#include <linux/syscalls.h>

unsigned int sysctl_nr_open __read_mostly = 1024*1024;
unsigned int sysctl_nr_open_min = BITS_PER_LONG;
unsigned int sysctl_nr_open_max = 1024*1024;

struct files_struct init_files;
void init_files_init(void) { }

int get_unused_fd_flags(unsigned flags) { return -EMFILE; }
EXPORT_SYMBOL(get_unused_fd_flags);

void put_unused_fd(unsigned int fd) { }
EXPORT_SYMBOL(put_unused_fd);

void fd_install(unsigned int fd, struct file *file) { }
EXPORT_SYMBOL(fd_install);

int close_fd(unsigned fd) { return -EBADF; }
EXPORT_SYMBOL(close_fd);

struct file *fget(unsigned int fd) { return NULL; }
EXPORT_SYMBOL(fget);

struct file *fget_raw(unsigned int fd) { return NULL; }
EXPORT_SYMBOL(fget_raw);

unsigned long __fdget(unsigned int fd) { return 0; }
EXPORT_SYMBOL(__fdget);

int receive_fd(struct file *file, unsigned int o_flags) { return -EINVAL; }
EXPORT_SYMBOL_GPL(receive_fd);

SYSCALL_DEFINE3(dup3, unsigned int, oldfd, unsigned int, newfd, int, flags) { return -ENOSYS; }
SYSCALL_DEFINE2(dup2, unsigned int, oldfd, unsigned int, newfd) { return -ENOSYS; }
SYSCALL_DEFINE1(dup, unsigned int, fildes) { return -ENOSYS; }
SYSCALL_DEFINE3(close_range, unsigned int, fd, unsigned int, max_fd, unsigned int, flags) { return -ENOSYS; }

int iterate_fd(struct files_struct *files, unsigned n,
	       int (*f)(const void *, struct file *, unsigned),
	       const void *p) { return 0; }
EXPORT_SYMBOL(iterate_fd);

void exit_files(struct task_struct *tsk) { }
struct files_struct *dup_fd(struct files_struct *oldf, unsigned nr, int *errorp) { return NULL; }
void put_files_struct(struct files_struct *files) { }
void do_close_on_exec(struct files_struct *files) { }
bool get_close_on_exec(unsigned int fd) { return false; }
unsigned long __fdget_raw(unsigned int fd) { return 0; }
