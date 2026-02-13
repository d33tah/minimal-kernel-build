
#include <linux/syscalls.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/sched/signal.h>
#include <linux/slab.h>
#include <linux/file.h>
#include <linux/fdtable.h>
#include <linux/bitops.h>
#include <linux/spinlock.h>
#include <linux/rcupdate.h>

#include "internal.h"

unsigned int sysctl_nr_open __read_mostly = 1024 * 1024;

static void __free_fdtable(struct fdtable *fdt)
{
	/* kvfree/kfree are no-ops (bump allocator) */
}

#define BITBIT_NR(nr) BITS_TO_LONGS(BITS_TO_LONGS(nr))
#define BITBIT_SIZE(nr) (BITBIT_NR(nr) * sizeof(long))

static void copy_fd_bitmaps(struct fdtable *nfdt, struct fdtable *ofdt,
			    unsigned int count)
{
	unsigned int cpy, set;

	cpy = count / BITS_PER_BYTE;
	set = (nfdt->max_fds - count) / BITS_PER_BYTE;
	memcpy(nfdt->open_fds, ofdt->open_fds, cpy);
	memset((char *)nfdt->open_fds + cpy, 0, set);
	memcpy(nfdt->close_on_exec, ofdt->close_on_exec, cpy);
	memset((char *)nfdt->close_on_exec + cpy, 0, set);

	cpy = BITBIT_SIZE(count);
	set = BITBIT_SIZE(nfdt->max_fds) - cpy;
	memcpy(nfdt->full_fds_bits, ofdt->full_fds_bits, cpy);
	memset((char *)nfdt->full_fds_bits + cpy, 0, set);
}

/* Simplified: init never exceeds NR_OPEN_DEFAULT fds, no expansion needed */
static int expand_files(struct files_struct *files, unsigned int nr)
	__releases(files->file_lock) __acquires(files->file_lock)
{
	struct fdtable *fdt = files_fdtable(files);

	if (nr < fdt->max_fds)
		return 0;
	return -EMFILE;
}

/* Simplified: open_files always fits in NR_OPEN_DEFAULT, no expansion needed */
struct files_struct *dup_fd(struct files_struct *oldf, unsigned int max_fds,
			    int *errorp)
{
	struct files_struct *newf;
	struct file **old_fds, **new_fds;
	unsigned int open_files, i;
	struct fdtable *old_fdt, *new_fdt;

	*errorp = -ENOMEM;
	newf = kmem_cache_alloc(files_cachep, GFP_KERNEL);
	if (!newf)
		return NULL;

	atomic_set(&newf->count, 1);
	spin_lock_init(&newf->file_lock);
	newf->resize_in_progress = false;
	init_waitqueue_head(&newf->resize_wait);
	newf->next_fd = 0;
	new_fdt = &newf->fdtab;
	new_fdt->max_fds = NR_OPEN_DEFAULT;
	new_fdt->close_on_exec = newf->close_on_exec_init;
	new_fdt->open_fds = newf->open_fds_init;
	new_fdt->full_fds_bits = newf->full_fds_bits_init;
	new_fdt->fd = &newf->fd_array[0];

	spin_lock(&oldf->file_lock);
	old_fdt = files_fdtable(oldf);
	open_files = old_fdt->max_fds;
	if (open_files > NR_OPEN_DEFAULT)
		open_files = NR_OPEN_DEFAULT;

	copy_fd_bitmaps(new_fdt, old_fdt, open_files);

	old_fds = old_fdt->fd;
	new_fds = new_fdt->fd;

	for (i = open_files; i != 0; i--) {
		struct file *f = *old_fds++;
		if (f) {
			get_file(f);
		} else {
			unsigned int fd = open_files - i;
			__clear_bit(fd, new_fdt->open_fds);
			__clear_bit(fd / BITS_PER_LONG, new_fdt->full_fds_bits);
		}
		rcu_assign_pointer(*new_fds++, f);
	}
	spin_unlock(&oldf->file_lock);

	memset(new_fds, 0,
	       (new_fdt->max_fds - open_files) * sizeof(struct file *));
	rcu_assign_pointer(newf->fdt, new_fdt);

	return newf;
}

static void put_files_struct(struct files_struct *files)
{
	if (atomic_dec_and_test(&files->count)) {
		struct fdtable *fdt = rcu_dereference_raw(files->fdt);
		unsigned int i, j = 0;

		for (;;) {
			unsigned long set;
			i = j * BITS_PER_LONG;
			if (i >= fdt->max_fds)
				break;
			set = fdt->open_fds[j++];
			while (set) {
				if (set & 1) {
					struct file *file =
						xchg(&fdt->fd[i], NULL);
					if (file) {
						filp_close(file, files);
						cond_resched();
					}
				}
				i++;
				set >>= 1;
			}
		}

		if (fdt != &files->fdtab)
			__free_fdtable(fdt);
		kmem_cache_free(files_cachep, files);
	}
}

void exit_files(struct task_struct *tsk)
{
	struct files_struct *files = tsk->files;

	if (files) {
		task_lock(tsk);
		tsk->files = NULL;
		task_unlock(tsk);
		put_files_struct(files);
	}
}

struct files_struct init_files = {
	.count		= ATOMIC_INIT(1),
	.fdt		= &init_files.fdtab,
	.fdtab		= {
		.max_fds	= NR_OPEN_DEFAULT,
		.fd		= &init_files.fd_array[0],
		.close_on_exec	= init_files.close_on_exec_init,
		.open_fds	= init_files.open_fds_init,
		.full_fds_bits	= init_files.full_fds_bits_init,
	},
	.file_lock	= __SPIN_LOCK_UNLOCKED(init_files.file_lock),
	.resize_wait	= __WAIT_QUEUE_HEAD_INITIALIZER(init_files.resize_wait),
};

int get_unused_fd_flags(unsigned flags)
{
	struct files_struct *files = current->files;
	unsigned int fd;
	int error;
	struct fdtable *fdt;
	unsigned int end = rlimit(RLIMIT_NOFILE);

	spin_lock(&files->file_lock);
repeat:
	fdt = files_fdtable(files);
	fd = 0;
	if (fd < files->next_fd)
		fd = files->next_fd;

	if (fd < fdt->max_fds) {
		unsigned int maxfd = fdt->max_fds;
		unsigned int maxbit = maxfd / BITS_PER_LONG;
		unsigned int bitbit = fd / BITS_PER_LONG;

		bitbit =
			find_next_zero_bit(fdt->full_fds_bits, maxbit, bitbit) *
			BITS_PER_LONG;
		if (bitbit <= maxfd) {
			if (bitbit > fd)
				fd = bitbit;
			fd = find_next_zero_bit(fdt->open_fds, maxfd, fd);
		} else {
			fd = maxfd;
		}
	}

	error = -EMFILE;
	if (fd >= end)
		goto out;

	error = expand_files(files, fd);
	if (error < 0)
		goto out;

	if (error)
		goto repeat;

	files->next_fd = fd + 1;

	__set_bit(fd, fdt->open_fds);
	{
		unsigned int fd_idx = fd / BITS_PER_LONG;
		if (!~fdt->open_fds[fd_idx])
			__set_bit(fd_idx, fdt->full_fds_bits);
	}
	if (flags & O_CLOEXEC)
		__set_bit(fd, fdt->close_on_exec);
	else if (test_bit(fd, fdt->close_on_exec))
		__clear_bit(fd, fdt->close_on_exec);
	error = fd;

out:
	spin_unlock(&files->file_lock);
	return error;
}

/* Simplified: resize never happens in minimal kernel */
void fd_install(unsigned int fd, struct file *file)
{
	struct files_struct *files = current->files;
	struct fdtable *fdt;

	rcu_read_lock_sched();
	fdt = rcu_dereference_sched(files->fdt);
	BUG_ON(fdt->fd[fd] != NULL);
	rcu_assign_pointer(fdt->fd[fd], file);
	rcu_read_unlock_sched();
}

static unsigned long __fdget(unsigned int fd)
{
	struct files_struct *files = current->files;
	struct file *file;

	/* Simplified: single-process, files->count always 1 */
	file = files_lookup_fd_raw(files, fd);
	if (!file || unlikely(file->f_mode & FMODE_PATH))
		return 0;
	return (unsigned long)file;
}

/* Stub: __fdget_raw not called in minimal kernel */
unsigned long __fdget_raw(unsigned int fd)
{
	return 0;
}

/* Simplified: file_count never > 1, no pos locking needed */
unsigned long __fdget_pos(unsigned int fd)
{
	return __fdget(fd);
}

void __f_unlock_pos(struct file *f)
{
}

/* dup/dup2/dup3 replaced with COND_SYSCALL */
