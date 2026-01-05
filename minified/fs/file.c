
#include <linux/syscalls.h>
#include <linux/export.h>
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
/* CLOSE_RANGE_UNSHARE, CLOSE_RANGE_CLOEXEC removed - unused */
#include <linux/security.h>

#include "internal.h"

unsigned int sysctl_nr_open __read_mostly = 1024 * 1024;

static void __free_fdtable(struct fdtable *fdt)
{
	kvfree(fdt->fd);
	kvfree(fdt->open_fds);
	kfree(fdt);
}

static void free_fdtable_rcu(struct rcu_head *rcu)
{
	__free_fdtable(container_of(rcu, struct fdtable, rcu));
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

static struct fdtable *alloc_fdtable(unsigned int nr)
{
	struct fdtable *fdt;
	void *data;

	nr /= (1024 / sizeof(struct file *));
	nr = roundup_pow_of_two(nr + 1);
	nr *= (1024 / sizeof(struct file *));
	nr = ALIGN(nr, BITS_PER_LONG);

	if (unlikely(nr > sysctl_nr_open))
		nr = ((sysctl_nr_open - 1) | (BITS_PER_LONG - 1)) + 1;

	fdt = kmalloc(sizeof(struct fdtable), GFP_KERNEL_ACCOUNT);
	if (!fdt)
		goto out;
	fdt->max_fds = nr;
	data = kvmalloc_array(nr, sizeof(struct file *), GFP_KERNEL_ACCOUNT);
	if (!data)
		goto out_fdt;
	fdt->fd = data;

	data = kvmalloc(max_t(size_t, 2 * nr / BITS_PER_BYTE + BITBIT_SIZE(nr),
			      L1_CACHE_BYTES),
			GFP_KERNEL_ACCOUNT);
	if (!data)
		goto out_arr;
	fdt->open_fds = data;
	data += nr / BITS_PER_BYTE;
	fdt->close_on_exec = data;
	data += nr / BITS_PER_BYTE;
	fdt->full_fds_bits = data;

	return fdt;

out_arr:
	kvfree(fdt->fd);
out_fdt:
	kfree(fdt);
out:
	return NULL;
}

/* expand_fdtable inlined into expand_files */

static int expand_files(struct files_struct *files, unsigned int nr)
	__releases(files->file_lock) __acquires(files->file_lock)
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
	/* Inlined expand_fdtable */
	{
		struct fdtable *new_fdt, *cur_fdt;

		spin_unlock(&files->file_lock);
		new_fdt = alloc_fdtable(nr);

		if (atomic_read(&files->count) > 1)
			synchronize_rcu();

		spin_lock(&files->file_lock);
		if (!new_fdt) {
			expanded = -ENOMEM;
		} else if (unlikely(new_fdt->max_fds <= nr)) {
			__free_fdtable(new_fdt);
			expanded = -EMFILE;
		} else {
			cur_fdt = files_fdtable(files);
			BUG_ON(nr < cur_fdt->max_fds);
			{
				size_t cpy = cur_fdt->max_fds *
					     sizeof(struct file *);
				size_t set =
					(new_fdt->max_fds - cur_fdt->max_fds) *
					sizeof(struct file *);
				BUG_ON(new_fdt->max_fds < cur_fdt->max_fds);
				memcpy(new_fdt->fd, cur_fdt->fd, cpy);
				memset((char *)new_fdt->fd + cpy, 0, set);
				copy_fd_bitmaps(new_fdt, cur_fdt,
						cur_fdt->max_fds);
			}
			rcu_assign_pointer(files->fdt, new_fdt);
			if (cur_fdt != &files->fdtab)
				call_rcu(&cur_fdt->rcu, free_fdtable_rcu);
			smp_wmb();
			expanded = 1;
		}
	}
	files->resize_in_progress = false;

	wake_up_all(&files->resize_wait);
	return expanded;
}

static inline void __clear_open_fd(unsigned int fd, struct fdtable *fdt)
{
	__clear_bit(fd, fdt->open_fds);
	__clear_bit(fd / BITS_PER_LONG, fdt->full_fds_bits);
}

static unsigned int sane_fdtable_size(struct fdtable *fdt, unsigned int max_fds)
{
	unsigned int size = fdt->max_fds;
	unsigned int i, count;

	for (i = size / BITS_PER_LONG; i > 0;) {
		if (fdt->open_fds[--i])
			break;
	}
	count = (i + 1) * BITS_PER_LONG;
	if (max_fds < NR_OPEN_DEFAULT)
		max_fds = NR_OPEN_DEFAULT;
	return ALIGN(min(count, max_fds), BITS_PER_LONG);
}

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
	open_files = sane_fdtable_size(old_fdt, max_fds);

	while (unlikely(open_files > new_fdt->max_fds)) {
		spin_unlock(&oldf->file_lock);

		if (new_fdt != &newf->fdtab)
			__free_fdtable(new_fdt);

		new_fdt = alloc_fdtable(open_files - 1);
		if (!new_fdt) {
			*errorp = -ENOMEM;
			goto out_release;
		}

		if (unlikely(new_fdt->max_fds < open_files)) {
			__free_fdtable(new_fdt);
			*errorp = -EMFILE;
			goto out_release;
		}

		spin_lock(&oldf->file_lock);
		old_fdt = files_fdtable(oldf);
		open_files = sane_fdtable_size(old_fdt, max_fds);
	}

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

	memset(new_fds, 0,
	       (new_fdt->max_fds - open_files) * sizeof(struct file *));

	rcu_assign_pointer(newf->fdt, new_fdt);

	return newf;

out_release:
	kmem_cache_free(files_cachep, newf);
out:
	return NULL;
}

static struct fdtable *close_files(struct files_struct *files)
{
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
				struct file *file = xchg(&fdt->fd[i], NULL);
				if (file) {
					filp_close(file, files);
					cond_resched();
				}
			}
			i++;
			set >>= 1;
		}
	}

	return fdt;
}

void put_files_struct(struct files_struct *files)
{
	if (atomic_dec_and_test(&files->count)) {
		struct fdtable *fdt = close_files(files);

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

	if (start <= files->next_fd)
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
	/* Debug #if 1 check removed - slot should never be non-NULL here */

out:
	spin_unlock(&files->file_lock);
	return error;
}

int get_unused_fd_flags(unsigned flags)
{
	return alloc_fd(0, rlimit(RLIMIT_NOFILE), flags);
}

static void __put_unused_fd(struct files_struct *files, unsigned int fd)
{
	struct fdtable *fdt = files_fdtable(files);
	__clear_open_fd(fd, fdt);
	if (fd < files->next_fd)
		files->next_fd = fd;
}

void put_unused_fd(unsigned int fd)
{
	struct files_struct *files = current->files;
	spin_lock(&files->file_lock);
	__put_unused_fd(files, fd);
	spin_unlock(&files->file_lock);
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

/* pick_file, close_fd, __close_range removed - never called */

void do_close_on_exec(struct files_struct *files)
{
	unsigned i;
	struct fdtable *fdt;

	spin_lock(&files->file_lock);
	for (i = 0;; i++) {
		unsigned long set;
		unsigned fd = i * BITS_PER_LONG;
		fdt = files_fdtable(files);
		if (fd >= fdt->max_fds)
			break;
		set = fdt->close_on_exec[i];
		if (!set)
			continue;
		fdt->close_on_exec[i] = 0;
		for (; set; fd++, set >>= 1) {
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

static inline struct file *__fget_files_rcu(struct files_struct *files,
					    unsigned int fd, fmode_t mask)
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

		if (unlikely(rcu_dereference_raw(files->fdt) != fdt) ||
		    unlikely(rcu_dereference_raw(*fdentry) != file)) {
			fput(file);
			continue;
		}

		return file;
	}
}

static struct file *__fget_files(struct files_struct *files, unsigned int fd,
				 fmode_t mask)
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

struct file *fget(unsigned int fd)
{
	return __fget(fd, FMODE_PATH);
}

unsigned long __fdget(unsigned int fd)
{
	struct files_struct *files = current->files;
	struct file *file;

	if (atomic_read(&files->count) == 1) {
		file = files_lookup_fd_raw(files, fd);
		if (!file || unlikely(file->f_mode & FMODE_PATH))
			return 0;
		return (unsigned long)file;
	} else {
		file = __fget(fd, FMODE_PATH);
		if (!file)
			return 0;
		return FDPUT_FPUT | (unsigned long)file;
	}
}

/* Stub: __fdget_raw not called in minimal kernel */
unsigned long __fdget_raw(unsigned int fd)
{
	return 0;
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

bool get_close_on_exec(unsigned int fd)
{
	struct files_struct *files = current->files;
	struct fdtable *fdt;
	bool res;
	rcu_read_lock();
	fdt = files_fdtable(files);
	res = close_on_exec(fd, fdt);
	rcu_read_unlock();
	return res;
}

/* Stub: dup syscalls not needed for Hello World */
SYSCALL_DEFINE3(dup3, unsigned int, oldfd, unsigned int, newfd, int, flags)
{
	return -ENOSYS;
}

SYSCALL_DEFINE2(dup2, unsigned int, oldfd, unsigned int, newfd)
{
	return -ENOSYS;
}

SYSCALL_DEFINE1(dup, unsigned int, fildes)
{
	return -ENOSYS;
}
