
#include <linux/fs.h>
#include <linux/sched/signal.h>
#include <linux/file.h>
#include <linux/fdtable.h>

#include "internal.h"


unsigned int sysctl_nr_open __read_mostly = 1024*1024;

#define BITBIT_NR(nr)	BITS_TO_LONGS(BITS_TO_LONGS(nr))
#define BITBIT_SIZE(nr)	(BITBIT_NR(nr) * sizeof(long))

static void copy_fd_bitmaps(struct fdtable *nfdt, struct fdtable *ofdt, unsigned int count)
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

/*
 * Runtime-dead: expand_files only reaches here when an fd index exceeds
 * the table size, which never happens on a single-shot boot (well under
 * NR_OPEN_DEFAULT fds). Link-live via expand_files; never executes.
 */
static int expand_fdtable(struct files_struct *files, unsigned int nr)
	__releases(files->file_lock)
	__acquires(files->file_lock)
{
	return -EMFILE;
}

static int expand_files(struct files_struct *files, unsigned int nr)
	__releases(files->file_lock)
	__acquires(files->file_lock)
{
	struct fdtable *fdt;
	int expanded = 0;

repeat:
	fdt = files_fdtable(files);

	 
	if (nr < fdt->max_fds)
		return expanded;

	 
	if (nr >= sysctl_nr_open)
		return -EMFILE;

	if (unlikely(files->resize_in_progress)) {
		spin_unlock(&files->file_lock);
		expanded = 1;
		wait_event(files->resize_wait, !files->resize_in_progress);
		spin_lock(&files->file_lock);
		goto repeat;
	}

	 
	files->resize_in_progress = true;
	expanded = expand_fdtable(files, nr);
	files->resize_in_progress = false;

	wake_up_all(&files->resize_wait);
	return expanded;
}

static inline void __set_close_on_exec(unsigned int fd, struct fdtable *fdt)
{
	__set_bit(fd, fdt->close_on_exec);
}

static inline void __clear_close_on_exec(unsigned int fd, struct fdtable *fdt)
{
	if (test_bit(fd, fdt->close_on_exec))
		__clear_bit(fd, fdt->close_on_exec);
}

static inline void __set_open_fd(unsigned int fd, struct fdtable *fdt)
{
	__set_bit(fd, fdt->open_fds);
	fd /= BITS_PER_LONG;
	if (!~fdt->open_fds[fd])
		__set_bit(fd, fdt->full_fds_bits);
}

static inline void __clear_open_fd(unsigned int fd, struct fdtable *fdt)
{
	__clear_bit(fd, fdt->open_fds);
	__clear_bit(fd / BITS_PER_LONG, fdt->full_fds_bits);
}

static unsigned int count_open_files(struct fdtable *fdt)
{
	unsigned int size = fdt->max_fds;
	unsigned int i;

	 
	for (i = size / BITS_PER_LONG; i > 0; ) {
		if (fdt->open_fds[--i])
			break;
	}
	i = (i + 1) * BITS_PER_LONG;
	return i;
}

static unsigned int sane_fdtable_size(struct fdtable *fdt)
{
	/*
	 * The sole caller (dup_fd) always passed max_fds == NR_OPEN_MAX
	 * (~0U), so the upstream min(count, max_fds) clamp and the
	 * "max_fds < NR_OPEN_DEFAULT" fixup were both no-ops.
	 */
	return ALIGN(count_open_files(fdt), BITS_PER_LONG);
}

struct files_struct *dup_fd(struct files_struct *oldf, int *errorp)
{
	struct files_struct *newf;
	struct file **old_fds, **new_fds;
	unsigned int open_files, i;
	struct fdtable *old_fdt, *new_fdt;

	*errorp = -ENOMEM;
	newf = kmem_cache_alloc(files_cachep, GFP_KERNEL);
	if (!newf)
		goto out;

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
	open_files = sane_fdtable_size(old_fdt);

	/*
	 * On this single-shot boot no fd table ever exceeds NR_OPEN_DEFAULT:
	 * init_files starts at that size and the only growth path
	 * (expand_fdtable) is a stub. So open_files (= ALIGN(count, 32),
	 * count <= old_fdt->max_fds == NR_OPEN_DEFAULT) is always
	 * <= new_fdt->max_fds and the resize loop is statically dead.
	 */

	copy_fd_bitmaps(new_fdt, old_fdt, open_files);

	old_fds = old_fdt->fd;
	new_fds = new_fdt->fd;

	for (i = open_files; i != 0; i--) {
		struct file *f = *old_fds++;
		if (f) {
			get_file(f);
		} else {
			 
			__clear_open_fd(open_files - i, new_fdt);
		}
		rcu_assign_pointer(*new_fds++, f);
	}
	spin_unlock(&oldf->file_lock);

	 
	memset(new_fds, 0, (new_fdt->max_fds - open_files) * sizeof(struct file *));

	rcu_assign_pointer(newf->fdt, new_fdt);

	return newf;

out:
	return NULL;
}

/*
 * Runtime-dead: nothing exits / closes its fd table on a single-shot boot.
 * Link-live via kernel/exit.c do_exit tail; never executes.
 */
void exit_files(struct task_struct *tsk)
{
}

struct files_struct init_files = { .count		= ATOMIC_INIT(1), .fdt		= &init_files.fdtab, .fdtab		= { .max_fds	= NR_OPEN_DEFAULT, .fd		= &init_files.fd_array[0], .close_on_exec	= init_files.close_on_exec_init, .open_fds	= init_files.open_fds_init, .full_fds_bits	= init_files.full_fds_bits_init, }, .file_lock	= __SPIN_LOCK_UNLOCKED(init_files.file_lock), .resize_wait	= __WAIT_QUEUE_HEAD_INITIALIZER(init_files.resize_wait), };

static unsigned int find_next_fd(struct fdtable *fdt, unsigned int start)
{
	unsigned int maxfd = fdt->max_fds;
	unsigned int maxbit = maxfd / BITS_PER_LONG;
	unsigned int bitbit = start / BITS_PER_LONG;

	bitbit = find_next_zero_bit(fdt->full_fds_bits, maxbit, bitbit) * BITS_PER_LONG;
	if (bitbit > maxfd)
		return maxfd;
	if (bitbit > start)
		start = bitbit;
	return find_next_zero_bit(fdt->open_fds, maxfd, start);
}

static int alloc_fd(unsigned start, unsigned end, unsigned flags)
{
	struct files_struct *files = current->files;
	unsigned int fd;
	int error;
	struct fdtable *fdt;

	spin_lock(&files->file_lock);
repeat:
	fdt = files_fdtable(files);
	fd = start;
	if (fd < files->next_fd)
		fd = files->next_fd;

	if (fd < fdt->max_fds)
		fd = find_next_fd(fdt, fd);

	 
	error = -EMFILE;
	if (fd >= end)
		goto out;

	error = expand_files(files, fd);
	if (error < 0)
		goto out;

	 
	if (error)
		goto repeat;

	if (start <= files->next_fd)
		files->next_fd = fd + 1;

	__set_open_fd(fd, fdt);
	if (flags & O_CLOEXEC)
		__set_close_on_exec(fd, fdt);
	else
		__clear_close_on_exec(fd, fdt);
	error = fd;
	if (rcu_access_pointer(fdt->fd[fd]) != NULL) {
		printk(KERN_WARNING "alloc_fd: slot %d not NULL!\n", fd);
		rcu_assign_pointer(fdt->fd[fd], NULL);
	}

out:
	spin_unlock(&files->file_lock);
	return error;
}

static int __get_unused_fd_flags(unsigned flags, unsigned long nofile)
{
	return alloc_fd(0, nofile, flags);
}

int get_unused_fd_flags(unsigned flags)
{
	return __get_unused_fd_flags(flags, rlimit(RLIMIT_NOFILE));
}

static void __put_unused_fd(struct files_struct *files, unsigned int fd)
{
	struct fdtable *fdt = files_fdtable(files);
	__clear_open_fd(fd, fdt);
	if (fd < files->next_fd)
		files->next_fd = fd;
}

void fd_install(unsigned int fd, struct file *file)
{
	struct files_struct *files = current->files;
	struct fdtable *fdt;

	rcu_read_lock_sched();

	if (unlikely(files->resize_in_progress)) {
		rcu_read_unlock_sched();
		spin_lock(&files->file_lock);
		fdt = files_fdtable(files);
		BUG_ON(fdt->fd[fd] != NULL);
		rcu_assign_pointer(fdt->fd[fd], file);
		spin_unlock(&files->file_lock);
		return;
	}
	 
	smp_rmb();
	fdt = rcu_dereference_sched(files->fdt);
	BUG_ON(fdt->fd[fd] != NULL);
	rcu_assign_pointer(fdt->fd[fd], file);
	rcu_read_unlock_sched();
}


void do_close_on_exec(struct files_struct *files)
{
	unsigned i;
	struct fdtable *fdt;

	 
	spin_lock(&files->file_lock);
	for (i = 0; ; i++) {
		unsigned long set;
		unsigned fd = i * BITS_PER_LONG;
		fdt = files_fdtable(files);
		if (fd >= fdt->max_fds)
			break;
		set = fdt->close_on_exec[i];
		if (!set)
			continue;
		fdt->close_on_exec[i] = 0;
		for ( ; set ; fd++, set >>= 1) {
			struct file *file;
			if (!(set & 1))
				continue;
			file = fdt->fd[fd];
			if (!file)
				continue;
			rcu_assign_pointer(fdt->fd[fd], NULL);
			__put_unused_fd(files, fd);
			spin_unlock(&files->file_lock);
			filp_close(file, files);
			cond_resched();
			spin_lock(&files->file_lock);
		}

	}
	spin_unlock(&files->file_lock);
}

static inline struct file *__fget_files_rcu(struct files_struct *files, unsigned int fd, fmode_t mask)
{
	for (;;) {
		struct file *file;
		struct fdtable *fdt = rcu_dereference_raw(files->fdt);
		struct file __rcu **fdentry;

		if (unlikely(fd >= fdt->max_fds))
			return NULL;

		fdentry = fdt->fd + array_index_nospec(fd, fdt->max_fds);
		file = rcu_dereference_raw(*fdentry);
		if (unlikely(!file))
			return NULL;

		if (unlikely(file->f_mode & mask))
			return NULL;

		 
		if (unlikely(!get_file_rcu(file)))
			continue;

		 
		if (unlikely(rcu_dereference_raw(files->fdt) != fdt) || unlikely(rcu_dereference_raw(*fdentry) != file)) {
			fput(file);
			continue;
		}

		 
		return file;
	}
}

static struct file *__fget_files(struct files_struct *files, unsigned int fd, fmode_t mask)
{
	struct file *file;

	rcu_read_lock();
	file = __fget_files_rcu(files, fd, mask);
	rcu_read_unlock();

	return file;
}

static inline struct file *__fget(unsigned int fd, fmode_t mask)
{
	return __fget_files(current->files, fd, mask);
}

static unsigned long __fget_light(unsigned int fd, fmode_t mask)
{
	struct files_struct *files = current->files;
	struct file *file;

	if (atomic_read(&files->count) == 1) {
		file = files_lookup_fd_raw(files, fd);
		if (!file || unlikely(file->f_mode & mask))
			return 0;
		return (unsigned long)file;
	} else {
		file = __fget(fd, mask);
		if (!file)
			return 0;
		return FDPUT_FPUT | (unsigned long)file;
	}
}
unsigned long __fdget(unsigned int fd)
{
	return __fget_light(fd, FMODE_PATH);
}

unsigned long __fdget_pos(unsigned int fd)
{
	unsigned long v = __fdget(fd);
	struct file *file = (struct file *)(v & ~3);

	if (file && (file->f_mode & FMODE_ATOMIC_POS)) {
		if (file_count(file) > 1) {
			v |= FDPUT_POS_UNLOCK;
			mutex_lock(&file->f_pos_lock);
		}
	}
	return v;
}

void __f_unlock_pos(struct file *f)
{
	mutex_unlock(&f->f_pos_lock);
}


