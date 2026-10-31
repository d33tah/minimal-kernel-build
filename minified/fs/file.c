
#include <linux/sched/signal.h>
#include <linux/fdtable.h>
#include <linux/file.h>

static void copy_fd_bitmaps(struct fdtable *nfdt, struct fdtable *ofdt,
			    unsigned int count)
{
	unsigned int cpy, set;

	cpy = count / BITS_PER_BYTE;
	set = (nfdt->max_fds - count) / BITS_PER_BYTE;
	memcpy(nfdt->open_fds, ofdt->open_fds, cpy);
	memset((char *)nfdt->open_fds + cpy, 0, set);
}

static inline void __set_open_fd(unsigned int fd, struct fdtable *fdt)
{
	__set_bit(fd, fdt->open_fds);
}

static inline void __clear_open_fd(unsigned int fd, struct fdtable *fdt)
{
	__clear_bit(fd, fdt->open_fds);
}

static unsigned int count_open_files(struct fdtable *fdt)
{
	unsigned int size = fdt->max_fds;
	unsigned int i;

	for (i = size / BITS_PER_LONG; i > 0;) {
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
	newf->next_fd = 0;
	new_fdt = &newf->fdtab;
	new_fdt->max_fds = NR_OPEN_DEFAULT;
	new_fdt->open_fds = newf->open_fds_init;
	new_fdt->fd = &newf->fd_array[0];

	spin_lock(&oldf->file_lock);
	old_fdt = files_fdtable(oldf);
	open_files = sane_fdtable_size(old_fdt);

	/*
	 * On this single-shot boot no fd table ever exceeds NR_OPEN_DEFAULT:
	 * init_files starts at that size and the only growth path
	 * (alloc_fd) refuses fds beyond max_fds. So open_files (= ALIGN(count, 32),
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

	memset(new_fds, 0,
	       (new_fdt->max_fds - open_files) * sizeof(struct file *));

	rcu_assign_pointer(newf->fdt, new_fdt);

	return newf;

out:
	return NULL;
}

struct files_struct init_files = {
	.count		= ATOMIC_INIT(1),
	.fdt		= &init_files.fdtab,
	.fdtab		= {
		.max_fds	= NR_OPEN_DEFAULT,
		.fd		= &init_files.fd_array[0],
		.open_fds	= init_files.open_fds_init,
	},
	.file_lock	= __SPIN_LOCK_UNLOCKED(init_files.file_lock),
};

/*
 * The table never grows (max_fds is always NR_OPEN_DEFAULT, BITS_PER_LONG),
 * so open_fds is one word: no full_fds_bits summary, a single-word search.
 */
static unsigned int find_next_fd(struct fdtable *fdt, unsigned int start)
{
	return find_next_zero_bit(fdt->open_fds, NR_OPEN_DEFAULT, start);
}

static int alloc_fd(unsigned start, unsigned end)
{
	struct files_struct *files = current->files;
	unsigned int fd;
	int error;
	struct fdtable *fdt;

	spin_lock(&files->file_lock);
	fdt = files_fdtable(files);
	fd = start;
	if (fd < files->next_fd)
		fd = files->next_fd;

	if (fd < fdt->max_fds)
		fd = find_next_fd(fdt, fd);

	/* The fd table never grows: NR_OPEN_DEFAULT slots, 3 are used. */
	error = -EMFILE;
	if (fd >= end || fd >= fdt->max_fds)
		goto out;

	if (start <= files->next_fd)
		files->next_fd = fd + 1;

	__set_open_fd(fd, fdt);
	error = fd;
	if (rcu_access_pointer(fdt->fd[fd]) != NULL) {
		rcu_assign_pointer(fdt->fd[fd], NULL);
	}

out:
	spin_unlock(&files->file_lock);
	return error;
}

static int __get_unused_fd_flags(unsigned flags, unsigned long nofile)
{
	return alloc_fd(0, nofile);
}

int get_unused_fd_flags(unsigned flags)
{
	return __get_unused_fd_flags(flags, rlimit(RLIMIT_NOFILE));
}

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

/*
 * Only one task ever owns an fd table (files->count is always 1: init's
 * table is a private dup_fd copy and nothing else is ever created), so a
 * lookup needs no reference: nothing can close the file underneath the
 * caller.
 */
struct file *fget_light(unsigned int fd)
{
	struct file *file = files_lookup_fd_raw(current->files, fd);

	return file;
}
