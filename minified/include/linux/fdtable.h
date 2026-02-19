
#ifndef __LINUX_FDTABLE_H
#define __LINUX_FDTABLE_H

#include <linux/rcupdate.h>
#include <asm/barrier.h>
struct task_struct;
#ifndef array_index_mask_nospec
						    unsigned long size)
{
	 
	OPTIMIZER_HIDE_VAR(index);
	return ~(long)(index | (size - 1UL - index)) >> (BITS_PER_LONG - 1);
}
#endif

#define array_index_nospec(index, size)					\
({									\
	typeof(index) _i = (index);					\
	typeof(size) _s = (size);					\
	unsigned long _mask = array_index_mask_nospec(_i, _s);		\
									\
	BUILD_BUG_ON(sizeof(_i) > sizeof(long));			\
	BUILD_BUG_ON(sizeof(_s) > sizeof(long));			\
									\
	(typeof(_i)) (_i & _mask);					\
})
#include <linux/fs.h>

#include <linux/atomic.h>

#define NR_OPEN_DEFAULT BITS_PER_LONG
#define NR_OPEN_MAX ~0U

struct fdtable {
	unsigned int max_fds;
	struct file __rcu **fd;       
	unsigned long *close_on_exec;
	unsigned long *open_fds;
	unsigned long *full_fds_bits;
	struct rcu_head rcu;
};

static inline bool close_on_exec(unsigned int fd, const struct fdtable *fdt)
{
	return test_bit(fd, fdt->close_on_exec);
}
struct files_struct {
   
	atomic_t count;
	bool resize_in_progress;
	wait_queue_head_t resize_wait;

	struct fdtable __rcu *fdt;
	struct fdtable fdtab;
   
	spinlock_t file_lock ____cacheline_aligned_in_smp;
	unsigned int next_fd;
	unsigned long close_on_exec_init[1];
	unsigned long open_fds_init[1];
	unsigned long full_fds_bits_init[1];
	struct file __rcu * fd_array[NR_OPEN_DEFAULT];
};

#define rcu_dereference_check_fdtable(files, fdtfd) \
	rcu_dereference_check((fdtfd), lockdep_is_held(&(files)->file_lock))

#define files_fdtable(files) \
	rcu_dereference_check_fdtable((files), (files)->fdt)

static inline struct file *files_lookup_fd_raw(struct files_struct *files, unsigned int fd)
{
	struct fdtable *fdt = rcu_dereference_raw(files->fdt);

	if (fd < fdt->max_fds) {
		fd = array_index_nospec(fd, fdt->max_fds);
		return rcu_dereference_raw(fdt->fd[fd]);
	}
	return NULL;
}

struct files_struct *dup_fd(struct files_struct *, unsigned, int *) __latent_entropy;

extern struct kmem_cache *files_cachep;

#endif  
