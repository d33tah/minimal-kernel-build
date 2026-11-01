#ifndef __LINUX_FDTABLE_H
#define __LINUX_FDTABLE_H

#include <linux/nospec.h>
#include <linux/fs.h>

#define NR_OPEN_DEFAULT BITS_PER_LONG

struct fdtable {
	unsigned int max_fds;
	struct file __rcu **fd;
	unsigned long *open_fds;
};

struct files_struct {
	atomic_t count;
	struct fdtable __rcu *fdt;
	struct fdtable fdtab;
	spinlock_t file_lock ____cacheline_aligned_in_smp;
	unsigned int next_fd;
	unsigned long open_fds_init[1];
	struct file __rcu *fd_array[NR_OPEN_DEFAULT];
};

#define rcu_dereference_check_fdtable(files, fdtfd) \
	rcu_dereference_check((fdtfd), lockdep_is_held(&(files)->file_lock))

#define files_fdtable(files) \
	rcu_dereference_check_fdtable((files), (files)->fdt)

static inline struct file *files_lookup_fd_raw(struct files_struct *files,
					       unsigned int fd)
{
	struct fdtable *fdt = rcu_dereference_raw(files->fdt);

	if (fd < fdt->max_fds) {
		fd = array_index_nospec(fd, fdt->max_fds);
		return rcu_dereference_raw(fdt->fd[fd]);
	}
	return NULL;
}

struct files_struct *dup_fd(struct files_struct *, int *) __latent_entropy;

extern struct kmem_cache *files_cachep;

#endif
