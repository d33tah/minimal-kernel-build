 

#include <linux/anon_inodes.h>
#include <linux/slab.h>
#include <linux/sched/autogroup.h>
#include <linux/sched/mm.h>
#include <linux/sched/coredump.h>
#include <linux/sched/user.h>
#include <linux/sched/numa_balancing.h>
#include <linux/sched/stat.h>
#include <linux/sched/task.h>
#include <linux/sched/task_stack.h>
#include <linux/sched/cputime.h>
#include <linux/seq_file.h>
#include <linux/rtmutex.h>
#include <linux/init.h>
#include <linux/unistd.h>
#include <linux/module.h>
#include <linux/vmalloc.h>
#include <linux/completion.h>
#include <linux/personality.h>
#include <linux/mempolicy.h>
#include <linux/sem.h>
#include <linux/file.h>
#include <linux/fdtable.h>
#include <linux/iocontext.h>
#include <linux/key.h>
#include <linux/binfmts.h>
#include <linux/mman.h>
#include <linux/mmu_notifier.h>
#include <linux/fs.h>
#include <linux/poll.h>
#include <linux/mm.h>
#include <linux/mm_inline.h>
#include <linux/vmacache.h>
#include <linux/nsproxy.h>
#include <linux/capability.h>
#include <linux/cpu.h>
#include <linux/cgroup.h>
#include <linux/security.h>
#include <linux/hugetlb.h>
#include <linux/seccomp.h>
#include <linux/swap.h>
#include <linux/syscalls.h>
#include <linux/jiffies.h>
#include <linux/futex.h>
#include <linux/compat.h>
#include <linux/kthread.h>
#include <linux/task_io_accounting_ops.h>
#include <linux/rcupdate.h>
#include <linux/ptrace.h>
#include <linux/mount.h>
#include <linux/audit.h>
#include <linux/memcontrol.h>
#include <linux/proc_fs.h>
#include <linux/rmap.h>
#include <linux/ksm.h>
#include <linux/acct.h>
#include <linux/userfaultfd_k.h>
#include <linux/tsacct_kern.h>
#include <linux/cn_proc.h>
#include <linux/freezer.h>
#include <linux/delayacct.h>
#include <linux/taskstats_kern.h>
#include <linux/random.h>
#include <linux/tty.h>
#include <linux/fs_struct.h>
#include <linux/magic.h>
#include <linux/perf_event.h>
#include <linux/posix-timers.h>
#include <linux/user-return-notifier.h>
#include <linux/oom.h>
#include <linux/khugepaged.h>
#include <linux/uprobes.h>
#include <linux/compiler.h>
#include <linux/sysctl.h>
#include <linux/kcov.h>
#include <linux/livepatch.h>
#include <linux/thread_info.h>
#include <linux/kasan.h>
#include <linux/scs.h>
#include <linux/io_uring.h>

#include <asm/pgalloc.h>
#include <linux/uaccess.h>
#include <asm/mmu_context.h>
#include <asm/cacheflush.h>
#include <asm/tlbflush.h>

#define MIN_THREADS 20

#define MAX_THREADS FUTEX_TID_MASK

unsigned long total_forks;	
int nr_threads;			

static int max_threads;		

#define NAMED_ARRAY_INDEX(x)	[x] = __stringify(x)

static const char * const resident_page_types[] = {
	NAMED_ARRAY_INDEX(MM_FILEPAGES),
	NAMED_ARRAY_INDEX(MM_ANONPAGES),
	NAMED_ARRAY_INDEX(MM_SWAPENTS),
	NAMED_ARRAY_INDEX(MM_SHMEMPAGES),
};

DEFINE_PER_CPU(unsigned long, process_counts) = 0;

__cacheline_aligned DEFINE_RWLOCK(tasklist_lock);  

int nr_processes(void)
{
	int cpu;
	int total = 0;

	for_each_possible_cpu(cpu)
		total += per_cpu(process_counts, cpu);

	return total;
}

void __weak arch_release_task_struct(struct task_struct *tsk)
{
}

#ifndef CONFIG_ARCH_TASK_STRUCT_ALLOCATOR
static struct kmem_cache *task_struct_cachep;

static inline struct task_struct *alloc_task_struct_node(int node)
{
	return kmem_cache_alloc_node(task_struct_cachep, GFP_KERNEL, node);
}

static inline void free_task_struct(struct task_struct *tsk)
{
	kmem_cache_free(task_struct_cachep, tsk);
}
#endif

#ifndef CONFIG_ARCH_THREAD_STACK_ALLOCATOR

# if THREAD_SIZE >= PAGE_SIZE || defined(CONFIG_VMAP_STACK)

static void thread_stack_free_rcu(struct rcu_head *rh)
{
	__free_pages(virt_to_page(rh), THREAD_SIZE_ORDER);
}

static void thread_stack_delayed_free(struct task_struct *tsk)
{
	struct rcu_head *rh = tsk->stack;

	call_rcu(rh, thread_stack_free_rcu);
}

static int alloc_thread_stack_node(struct task_struct *tsk, int node)
{
	struct page *page = alloc_pages_node(node, THREADINFO_GFP,
					     THREAD_SIZE_ORDER);

	if (likely(page)) {
		tsk->stack = kasan_reset_tag(page_address(page));
		return 0;
	}
	return -ENOMEM;
}

static void free_thread_stack(struct task_struct *tsk)
{
	thread_stack_delayed_free(tsk);
	tsk->stack = NULL;
}

# else 

static struct kmem_cache *thread_stack_cache;

static void thread_stack_free_rcu(struct rcu_head *rh)
{
	kmem_cache_free(thread_stack_cache, rh);
}

static void thread_stack_delayed_free(struct task_struct *tsk)
{
	struct rcu_head *rh = tsk->stack;

	call_rcu(rh, thread_stack_free_rcu);
}

static int alloc_thread_stack_node(struct task_struct *tsk, int node)
{
	unsigned long *stack;
	stack = kmem_cache_alloc_node(thread_stack_cache, THREADINFO_GFP, node);
	stack = kasan_reset_tag(stack);
	tsk->stack = stack;
	return stack ? 0 : -ENOMEM;
}

static void free_thread_stack(struct task_struct *tsk)
{
	thread_stack_delayed_free(tsk);
	tsk->stack = NULL;
}

void thread_stack_cache_init(void)
{
	thread_stack_cache = kmem_cache_create_usercopy("thread_stack",
					THREAD_SIZE, THREAD_SIZE, 0, 0,
					THREAD_SIZE, NULL);
	BUG_ON(thread_stack_cache == NULL);
}

# endif 
#else 

static int alloc_thread_stack_node(struct task_struct *tsk, int node)
{
	unsigned long *stack;

	stack = arch_alloc_thread_stack_node(tsk, node);
	tsk->stack = stack;
	return stack ? 0 : -ENOMEM;
}

static void free_thread_stack(struct task_struct *tsk)
{
	arch_free_thread_stack(tsk);
	tsk->stack = NULL;
}

#endif 

static struct kmem_cache *signal_cachep;

struct kmem_cache *sighand_cachep;

struct kmem_cache *files_cachep;

struct kmem_cache *fs_cachep;

static struct kmem_cache *vm_area_cachep;

static struct kmem_cache *mm_cachep;

struct vm_area_struct *vm_area_alloc(struct mm_struct *mm)
{
	struct vm_area_struct *vma;

	vma = kmem_cache_alloc(vm_area_cachep, GFP_KERNEL);
	if (vma)
		vma_init(vma, mm);
	return vma;
}

struct vm_area_struct *vm_area_dup(struct vm_area_struct *orig)
{
	struct vm_area_struct *new = kmem_cache_alloc(vm_area_cachep, GFP_KERNEL);

	if (new) {
		ASSERT_EXCLUSIVE_WRITER(orig->vm_flags);
		ASSERT_EXCLUSIVE_WRITER(orig->vm_file);
		
		*new = data_race(*orig);
		INIT_LIST_HEAD(&new->anon_vma_chain);
		new->vm_next = new->vm_prev = NULL;
		dup_anon_vma_name(orig, new);
	}
	return new;
}

void vm_area_free(struct vm_area_struct *vma)
{
	free_anon_vma_name(vma);
	kmem_cache_free(vm_area_cachep, vma);
}

static void account_kernel_stack(struct task_struct *tsk, int account)
{
	if (IS_ENABLED(CONFIG_VMAP_STACK)) {
		struct vm_struct *vm = task_stack_vm_area(tsk);
		int i;

		for (i = 0; i < THREAD_SIZE / PAGE_SIZE; i++)
			mod_lruvec_page_state(vm->pages[i], NR_KERNEL_STACK_KB,
					      account * (PAGE_SIZE / 1024));
	} else {
		void *stack = task_stack_page(tsk);

		
		mod_lruvec_kmem_state(stack, NR_KERNEL_STACK_KB,
				      account * (THREAD_SIZE / 1024));
	}
}

void exit_task_stack_account(struct task_struct *tsk)
{
	account_kernel_stack(tsk, -1);

	if (IS_ENABLED(CONFIG_VMAP_STACK)) {
		struct vm_struct *vm;
		int i;

		vm = task_stack_vm_area(tsk);
		for (i = 0; i < THREAD_SIZE / PAGE_SIZE; i++)
			memcg_kmem_uncharge_page(vm->pages[i], 0);
	}
}

static void release_task_stack(struct task_struct *tsk)
{
	if (WARN_ON(READ_ONCE(tsk->__state) != TASK_DEAD))
		return;  

	free_thread_stack(tsk);
}

void put_task_stack(struct task_struct *tsk)
{
	if (refcount_dec_and_test(&tsk->stack_refcount))
		release_task_stack(tsk);
}

void free_task(struct task_struct *tsk)
{
	release_user_cpus_ptr(tsk);
	scs_release(tsk);


	WARN_ON_ONCE(refcount_read(&tsk->stack_refcount) != 0);
	rt_mutex_debug_task_free(tsk);
	arch_release_task_struct(tsk);
	if (tsk->flags & PF_KTHREAD)
		free_kthread_struct(tsk);
	free_task_struct(tsk);
}

static void dup_mm_exe_file(struct mm_struct *mm, struct mm_struct *oldmm)
{
	struct file *exe_file;

	exe_file = get_mm_exe_file(oldmm);
	RCU_INIT_POINTER(mm->exe_file, exe_file);

	if (exe_file)
		deny_write_access(exe_file);
}

static __latent_entropy int dup_mmap(struct mm_struct *mm,
					struct mm_struct *oldmm)
{
	/* Minimal stub: simplified VMA duplication for fork */
	struct vm_area_struct *mpnt, *tmp, *prev, **pprev;
	struct rb_node **rb_link, *rb_parent;
	int retval = 0;

	if (mmap_write_lock_killable(oldmm))
		return -EINTR;

	mmap_write_lock_nested(mm, SINGLE_DEPTH_NESTING);

	dup_mm_exe_file(mm, oldmm);
	mm->total_vm = oldmm->total_vm;

	rb_link = &mm->mm_rb.rb_node;
	rb_parent = NULL;
	pprev = &mm->mmap;
	prev = NULL;

	for (mpnt = oldmm->mmap; mpnt; mpnt = mpnt->vm_next) {
		if (mpnt->vm_flags & VM_DONTCOPY)
			continue;

		tmp = vm_area_dup(mpnt);
		if (!tmp) {
			retval = -ENOMEM;
			break;
		}

		tmp->vm_mm = mm;
		tmp->vm_flags &= ~(VM_LOCKED | VM_LOCKONFAULT);

		if (tmp->vm_file)
			get_file(tmp->vm_file);

		*pprev = tmp;
		pprev = &tmp->vm_next;
		tmp->vm_prev = prev;
		prev = tmp;

		__vma_link_rb(mm, tmp, rb_link, rb_parent);
		rb_link = &tmp->vm_rb.rb_right;
		rb_parent = &tmp->vm_rb;
		mm->map_count++;
	}

	mmap_write_unlock(mm);
	mmap_write_unlock(oldmm);
	return retval;
}

static inline int mm_alloc_pgd(struct mm_struct *mm)
{
	mm->pgd = pgd_alloc(mm);
	if (unlikely(!mm->pgd))
		return -ENOMEM;
	return 0;
}

static inline void mm_free_pgd(struct mm_struct *mm)
{
	pgd_free(mm, mm->pgd);
}

static void check_mm(struct mm_struct *mm)
{
	int i;

	BUILD_BUG_ON_MSG(ARRAY_SIZE(resident_page_types) != NR_MM_COUNTERS,
			 "Please make sure 'struct resident_page_types[]' is updated as well");

	for (i = 0; i < NR_MM_COUNTERS; i++) {
		long x = atomic_long_read(&mm->rss_stat.count[i]);

		if (unlikely(x))
			pr_alert("BUG: Bad rss-counter state mm:%p type:%s val:%ld\n",
				 mm, resident_page_types[i], x);
	}

	if (mm_pgtables_bytes(mm))
		pr_alert("BUG: non-zero pgtables_bytes on freeing mm: %ld\n",
				mm_pgtables_bytes(mm));

}

#define allocate_mm()	(kmem_cache_alloc(mm_cachep, GFP_KERNEL))
#define free_mm(mm)	(kmem_cache_free(mm_cachep, (mm)))

void __mmdrop(struct mm_struct *mm)
{
	BUG_ON(mm == &init_mm);
	WARN_ON_ONCE(mm == current->mm);
	WARN_ON_ONCE(mm == current->active_mm);
	mm_free_pgd(mm);
	destroy_context(mm);
	mmu_notifier_subscriptions_destroy(mm);
	check_mm(mm);
	put_user_ns(mm->user_ns);
	mm_pasid_drop(mm);
	free_mm(mm);
}

static void mmdrop_async_fn(struct work_struct *work)
{
	struct mm_struct *mm;

	mm = container_of(work, struct mm_struct, async_put_work);
	__mmdrop(mm);
}

static void mmdrop_async(struct mm_struct *mm)
{
	if (unlikely(atomic_dec_and_test(&mm->mm_count))) {
		INIT_WORK(&mm->async_put_work, mmdrop_async_fn);
		schedule_work(&mm->async_put_work);
	}
}

static inline void free_signal_struct(struct signal_struct *sig)
{
	taskstats_tgid_free(sig);
	sched_autogroup_exit(sig);
	
	if (sig->oom_mm)
		mmdrop_async(sig->oom_mm);
	kmem_cache_free(signal_cachep, sig);
}

static inline void put_signal_struct(struct signal_struct *sig)
{
	if (refcount_dec_and_test(&sig->sigcnt))
		free_signal_struct(sig);
}

void __put_task_struct(struct task_struct *tsk)
{
	WARN_ON(!tsk->exit_state);
	WARN_ON(refcount_read(&tsk->usage));
	WARN_ON(tsk == current);

	io_uring_free(tsk);
	cgroup_free(tsk);
	task_numa_free(tsk, true);
	security_task_free(tsk);
	 
	exit_creds(tsk);
	delayacct_tsk_free(tsk);
	put_signal_struct(tsk->signal);
	sched_core_free(tsk);
	free_task(tsk);
}

void __init __weak arch_task_cache_init(void) { }

static void set_max_threads(unsigned int max_threads_suggested)
{
	u64 threads;
	unsigned long nr_pages = totalram_pages();

	
	if (fls64(nr_pages) + fls64(PAGE_SIZE) > 64)
		threads = MAX_THREADS;
	else
		threads = div64_u64((u64) nr_pages * (u64) PAGE_SIZE,
				    (u64) THREAD_SIZE * 8UL);

	if (threads > max_threads_suggested)
		threads = max_threads_suggested;

	max_threads = clamp_t(u64, threads, MIN_THREADS, MAX_THREADS);
}

int arch_task_struct_size __read_mostly;

#ifndef CONFIG_ARCH_TASK_STRUCT_ALLOCATOR
static void task_struct_whitelist(unsigned long *offset, unsigned long *size)
{
	
	arch_thread_struct_whitelist(offset, size);

	
	if (unlikely(*size == 0))
		*offset = 0;
	else
		*offset += offsetof(struct task_struct, thread);
}
#endif 

void __init fork_init(void)
{
	int i;
#ifndef CONFIG_ARCH_TASK_STRUCT_ALLOCATOR
#ifndef ARCH_MIN_TASKALIGN
#define ARCH_MIN_TASKALIGN	0
#endif
	int align = max_t(int, L1_CACHE_BYTES, ARCH_MIN_TASKALIGN);
	unsigned long useroffset, usersize;

	
	task_struct_whitelist(&useroffset, &usersize);
	task_struct_cachep = kmem_cache_create_usercopy("task_struct",
			arch_task_struct_size, align,
			SLAB_PANIC|SLAB_ACCOUNT,
			useroffset, usersize, NULL);
#endif

	
	arch_task_cache_init();

	set_max_threads(MAX_THREADS);

	init_task.signal->rlim[RLIMIT_NPROC].rlim_cur = max_threads/2;
	init_task.signal->rlim[RLIMIT_NPROC].rlim_max = max_threads/2;
	init_task.signal->rlim[RLIMIT_SIGPENDING] =
		init_task.signal->rlim[RLIMIT_NPROC];

	for (i = 0; i < MAX_PER_NAMESPACE_UCOUNTS; i++)
		init_user_ns.ucount_max[i] = max_threads/2;

	set_rlimit_ucount_max(&init_user_ns, UCOUNT_RLIMIT_NPROC,      RLIM_INFINITY);
	set_rlimit_ucount_max(&init_user_ns, UCOUNT_RLIMIT_MSGQUEUE,   RLIM_INFINITY);
	set_rlimit_ucount_max(&init_user_ns, UCOUNT_RLIMIT_SIGPENDING, RLIM_INFINITY);
	set_rlimit_ucount_max(&init_user_ns, UCOUNT_RLIMIT_MEMLOCK,    RLIM_INFINITY);

	scs_init();

	lockdep_init_task(&init_task);
	uprobes_init();
}

int __weak arch_dup_task_struct(struct task_struct *dst,
					       struct task_struct *src)
{
	*dst = *src;
	return 0;
}

void set_task_stack_end_magic(struct task_struct *tsk)
{
	unsigned long *stackend;

	stackend = end_of_stack(tsk);
	*stackend = STACK_END_MAGIC;	
}

static struct task_struct *dup_task_struct(struct task_struct *orig, int node)
{
	struct task_struct *tsk;
	int err;

	if (node == NUMA_NO_NODE)
		node = tsk_fork_get_node(orig);
	tsk = alloc_task_struct_node(node);
	if (!tsk)
		return NULL;

	err = arch_dup_task_struct(tsk, orig);
	if (err)
		goto free_tsk;

	err = alloc_thread_stack_node(tsk, node);
	if (err)
		goto free_tsk;

	refcount_set(&tsk->stack_refcount, 1);
	account_kernel_stack(tsk, 1);

	err = scs_prepare(tsk, node);
	if (err)
		goto free_stack;

	setup_thread_stack(tsk, orig);
	clear_user_return_notifier(tsk);
	clear_tsk_need_resched(tsk);
	set_task_stack_end_magic(tsk);
	clear_syscall_work_syscall_user_dispatch(tsk);

	if (orig->cpus_ptr == &orig->cpus_mask)
		tsk->cpus_ptr = &tsk->cpus_mask;
	dup_user_cpus_ptr(tsk, orig, node);

	
	refcount_set(&tsk->rcu_users, 2);
	
	refcount_set(&tsk->usage, 1);
	tsk->splice_pipe = NULL;
	tsk->task_frag.page = NULL;
	tsk->wake_q.next = NULL;
	tsk->worker_private = NULL;

	kcov_task_init(tsk);
	kmap_local_fork(tsk);

	tsk->reported_split_lock = 0;

	return tsk;

free_stack:
	exit_task_stack_account(tsk);
	free_thread_stack(tsk);
free_tsk:
	free_task_struct(tsk);
	return NULL;
}

__cacheline_aligned_in_smp DEFINE_SPINLOCK(mmlist_lock);

static unsigned long default_dump_filter = MMF_DUMP_FILTER_DEFAULT;

static int __init coredump_filter_setup(char *s)
{
	default_dump_filter =
		(simple_strtoul(s, NULL, 0) << MMF_DUMP_FILTER_SHIFT) &
		MMF_DUMP_FILTER_MASK;
	return 1;
}

__setup("coredump_filter=", coredump_filter_setup);

#include <linux/init_task.h>

static void mm_init_aio(struct mm_struct *mm)
{
}

static __always_inline void mm_clear_owner(struct mm_struct *mm,
					   struct task_struct *p)
{
}

static void mm_init_owner(struct mm_struct *mm, struct task_struct *p)
{
}

static void mm_init_uprobes_state(struct mm_struct *mm)
{
}

static struct mm_struct *mm_init(struct mm_struct *mm, struct task_struct *p,
	struct user_namespace *user_ns)
{
	mm->mmap = NULL;
	mm->mm_rb = RB_ROOT;
	mm->vmacache_seqnum = 0;
	atomic_set(&mm->mm_users, 1);
	atomic_set(&mm->mm_count, 1);
	seqcount_init(&mm->write_protect_seq);
	mmap_init_lock(mm);
	INIT_LIST_HEAD(&mm->mmlist);
	mm_pgtables_bytes_init(mm);
	mm->map_count = 0;
	mm->locked_vm = 0;
	atomic64_set(&mm->pinned_vm, 0);
	memset(&mm->rss_stat, 0, sizeof(mm->rss_stat));
	spin_lock_init(&mm->page_table_lock);
	spin_lock_init(&mm->arg_lock);
	mm_init_cpumask(mm);
	mm_init_aio(mm);
	mm_init_owner(mm, p);
	mm_pasid_init(mm);
	RCU_INIT_POINTER(mm->exe_file, NULL);
	mmu_notifier_subscriptions_init(mm);
	init_tlb_flush_pending(mm);
	mm_init_uprobes_state(mm);
	hugetlb_count_init(mm);

	if (current->mm) {
		mm->flags = current->mm->flags & MMF_INIT_MASK;
		mm->def_flags = current->mm->def_flags & VM_INIT_DEF_MASK;
	} else {
		mm->flags = default_dump_filter;
		mm->def_flags = 0;
	}

	if (mm_alloc_pgd(mm))
		goto fail_nopgd;

	if (init_new_context(p, mm))
		goto fail_nocontext;

	mm->user_ns = get_user_ns(user_ns);
	return mm;

fail_nocontext:
	mm_free_pgd(mm);
fail_nopgd:
	free_mm(mm);
	return NULL;
}

struct mm_struct *mm_alloc(void)
{
	struct mm_struct *mm;

	mm = allocate_mm();
	if (!mm)
		return NULL;

	memset(mm, 0, sizeof(*mm));
	return mm_init(mm, current, current_user_ns());
}

static inline void __mmput(struct mm_struct *mm)
{
	VM_BUG_ON(atomic_read(&mm->mm_users));

	uprobe_clear_state(mm);
	ksm_exit(mm);
	khugepaged_exit(mm); 
	exit_mmap(mm);
	mm_put_huge_zero_page(mm);
	set_mm_exe_file(mm, NULL);
	if (!list_empty(&mm->mmlist)) {
		spin_lock(&mmlist_lock);
		list_del(&mm->mmlist);
		spin_unlock(&mmlist_lock);
	}
	if (mm->binfmt)
		module_put(mm->binfmt->module);
	mmdrop(mm);
}

void mmput(struct mm_struct *mm)
{
	might_sleep();

	if (atomic_dec_and_test(&mm->mm_users))
		__mmput(mm);
}

static void mmput_async_fn(struct work_struct *work)
{
	struct mm_struct *mm = container_of(work, struct mm_struct,
					    async_put_work);

	__mmput(mm);
}

void mmput_async(struct mm_struct *mm)
{
	if (atomic_dec_and_test(&mm->mm_users)) {
		INIT_WORK(&mm->async_put_work, mmput_async_fn);
		schedule_work(&mm->async_put_work);
	}
}

int set_mm_exe_file(struct mm_struct *mm, struct file *new_exe_file)
{
	struct file *old_exe_file;

	
	old_exe_file = rcu_dereference_raw(mm->exe_file);

	if (new_exe_file) {
		
		if (unlikely(deny_write_access(new_exe_file)))
			return -EACCES;
		get_file(new_exe_file);
	}
	rcu_assign_pointer(mm->exe_file, new_exe_file);
	if (old_exe_file) {
		allow_write_access(old_exe_file);
		fput(old_exe_file);
	}
	return 0;
}

int replace_mm_exe_file(struct mm_struct *mm, struct file *new_exe_file)
{
	/* Stubbed: exe file replacement not needed for minimal boot */
	return -EINVAL;
}

struct file *get_mm_exe_file(struct mm_struct *mm)
{
	struct file *exe_file;

	rcu_read_lock();
	exe_file = rcu_dereference(mm->exe_file);
	if (exe_file && !get_file_rcu(exe_file))
		exe_file = NULL;
	rcu_read_unlock();
	return exe_file;
}

struct file *get_task_exe_file(struct task_struct *task)
{
	struct file *exe_file = NULL;
	struct mm_struct *mm;

	task_lock(task);
	mm = task->mm;
	if (mm) {
		if (!(task->flags & PF_KTHREAD))
			exe_file = get_mm_exe_file(mm);
	}
	task_unlock(task);
	return exe_file;
}

struct mm_struct *get_task_mm(struct task_struct *task)
{
	struct mm_struct *mm;

	task_lock(task);
	mm = task->mm;
	if (mm) {
		if (task->flags & PF_KTHREAD)
			mm = NULL;
		else
			mmget(mm);
	}
	task_unlock(task);
	return mm;
}

struct mm_struct *mm_access(struct task_struct *task, unsigned int mode)
{
	struct mm_struct *mm;
	int err;

	err =  down_read_killable(&task->signal->exec_update_lock);
	if (err)
		return ERR_PTR(err);

	mm = get_task_mm(task);
	if (mm && mm != current->mm &&
			!ptrace_may_access(task, mode)) {
		mmput(mm);
		mm = ERR_PTR(-EACCES);
	}
	up_read(&task->signal->exec_update_lock);

	return mm;
}

static void complete_vfork_done(struct task_struct *tsk)
{
	struct completion *vfork;

	task_lock(tsk);
	vfork = tsk->vfork_done;
	if (likely(vfork)) {
		tsk->vfork_done = NULL;
		complete(vfork);
	}
	task_unlock(tsk);
}

static int wait_for_vfork_done(struct task_struct *child,
				struct completion *vfork)
{
	int killed;

	freezer_do_not_count();
	cgroup_enter_frozen();
	killed = wait_for_completion_killable(vfork);
	cgroup_leave_frozen(false);
	freezer_count();

	if (killed) {
		task_lock(child);
		child->vfork_done = NULL;
		task_unlock(child);
	}

	put_task_struct(child);
	return killed;
}

static void mm_release(struct task_struct *tsk, struct mm_struct *mm)
{
	uprobe_free_utask(tsk);

	
	deactivate_mm(tsk, mm);

	
	if (tsk->clear_child_tid) {
		if (atomic_read(&mm->mm_users) > 1) {
			
			put_user(0, tsk->clear_child_tid);
			do_futex(tsk->clear_child_tid, FUTEX_WAKE,
					1, NULL, NULL, 0, 0);
		}
		tsk->clear_child_tid = NULL;
	}

	
	if (tsk->vfork_done)
		complete_vfork_done(tsk);
}

void exit_mm_release(struct task_struct *tsk, struct mm_struct *mm)
{
	futex_exit_release(tsk);
	mm_release(tsk, mm);
}

void exec_mm_release(struct task_struct *tsk, struct mm_struct *mm)
{
	futex_exec_release(tsk);
	mm_release(tsk, mm);
}

static struct mm_struct *dup_mm(struct task_struct *tsk,
				struct mm_struct *oldmm)
{
	struct mm_struct *mm;
	int err;

	mm = allocate_mm();
	if (!mm)
		goto fail_nomem;

	memcpy(mm, oldmm, sizeof(*mm));

	if (!mm_init(mm, tsk, mm->user_ns))
		goto fail_nomem;

	err = dup_mmap(mm, oldmm);
	if (err)
		goto free_pt;

	mm->hiwater_rss = get_mm_rss(mm);
	mm->hiwater_vm = mm->total_vm;

	if (mm->binfmt && !try_module_get(mm->binfmt->module))
		goto free_pt;

	return mm;

free_pt:
	
	mm->binfmt = NULL;
	mm_init_owner(mm, NULL);
	mmput(mm);

fail_nomem:
	return NULL;
}

static int copy_mm(unsigned long clone_flags, struct task_struct *tsk)
{
	struct mm_struct *mm, *oldmm;

	tsk->min_flt = tsk->maj_flt = 0;
	tsk->nvcsw = tsk->nivcsw = 0;

	tsk->mm = NULL;
	tsk->active_mm = NULL;

	
	oldmm = current->mm;
	if (!oldmm)
		return 0;

	
	vmacache_flush(tsk);

	if (clone_flags & CLONE_VM) {
		mmget(oldmm);
		mm = oldmm;
	} else {
		mm = dup_mm(tsk, current->mm);
		if (!mm)
			return -ENOMEM;
	}

	tsk->mm = mm;
	tsk->active_mm = mm;
	return 0;
}

static int copy_fs(unsigned long clone_flags, struct task_struct *tsk)
{
	struct fs_struct *fs = current->fs;
	if (clone_flags & CLONE_FS) {
		
		spin_lock(&fs->lock);
		if (fs->in_exec) {
			spin_unlock(&fs->lock);
			return -EAGAIN;
		}
		fs->users++;
		spin_unlock(&fs->lock);
		return 0;
	}
	tsk->fs = copy_fs_struct(fs);
	if (!tsk->fs)
		return -ENOMEM;
	return 0;
}

static int copy_files(unsigned long clone_flags, struct task_struct *tsk)
{
	struct files_struct *oldf, *newf;
	int error = 0;

	
	oldf = current->files;
	if (!oldf)
		goto out;

	if (clone_flags & CLONE_FILES) {
		atomic_inc(&oldf->count);
		goto out;
	}

	newf = dup_fd(oldf, NR_OPEN_MAX, &error);
	if (!newf)
		goto out;

	tsk->files = newf;
	error = 0;
out:
	return error;
}

static int copy_sighand(unsigned long clone_flags, struct task_struct *tsk)
{
	struct sighand_struct *sig;

	if (clone_flags & CLONE_SIGHAND) {
		refcount_inc(&current->sighand->count);
		return 0;
	}
	sig = kmem_cache_alloc(sighand_cachep, GFP_KERNEL);
	RCU_INIT_POINTER(tsk->sighand, sig);
	if (!sig)
		return -ENOMEM;

	refcount_set(&sig->count, 1);
	spin_lock_irq(&current->sighand->siglock);
	memcpy(sig->action, current->sighand->action, sizeof(sig->action));
	spin_unlock_irq(&current->sighand->siglock);

	
	if (clone_flags & CLONE_CLEAR_SIGHAND)
		flush_signal_handlers(tsk, 0);

	return 0;
}

void __cleanup_sighand(struct sighand_struct *sighand)
{
	if (refcount_dec_and_test(&sighand->count)) {
		kmem_cache_free(sighand_cachep, sighand);
	}
}

static void posix_cpu_timers_init_group(struct signal_struct *sig)
{
	struct posix_cputimers *pct = &sig->posix_cputimers;
	unsigned long cpu_limit;

	cpu_limit = READ_ONCE(sig->rlim[RLIMIT_CPU].rlim_cur);
	posix_cputimers_group_init(pct, cpu_limit);
}

static int copy_signal(unsigned long clone_flags, struct task_struct *tsk)
{
	struct signal_struct *sig;

	if (clone_flags & CLONE_THREAD)
		return 0;

	sig = kmem_cache_zalloc(signal_cachep, GFP_KERNEL);
	tsk->signal = sig;
	if (!sig)
		return -ENOMEM;

	sig->nr_threads = 1;
	atomic_set(&sig->live, 1);
	refcount_set(&sig->sigcnt, 1);

	
	sig->thread_head = (struct list_head)LIST_HEAD_INIT(tsk->thread_node);
	tsk->thread_node = (struct list_head)LIST_HEAD_INIT(sig->thread_head);

	init_waitqueue_head(&sig->wait_chldexit);
	sig->curr_target = tsk;
	init_sigpending(&sig->shared_pending);
	INIT_HLIST_HEAD(&sig->multiprocess);
	seqlock_init(&sig->stats_lock);
	prev_cputime_init(&sig->prev_cputime);

	task_lock(current->group_leader);
	memcpy(sig->rlim, current->signal->rlim, sizeof sig->rlim);
	task_unlock(current->group_leader);

	posix_cpu_timers_init_group(sig);

	tty_audit_fork(sig);
	sched_autogroup_fork(sig);

	sig->oom_score_adj = current->signal->oom_score_adj;
	sig->oom_score_adj_min = current->signal->oom_score_adj_min;

	mutex_init(&sig->cred_guard_mutex);
	init_rwsem(&sig->exec_update_lock);

	return 0;
}

static void copy_seccomp(struct task_struct *p)
{
}

SYSCALL_DEFINE1(set_tid_address, int __user *, tidptr)
{
	current->clear_child_tid = tidptr;

	return task_pid_vnr(current);
}

static void rt_mutex_init_task(struct task_struct *p)
{
	raw_spin_lock_init(&p->pi_lock);
}

static inline void init_task_pid_links(struct task_struct *task)
{
	enum pid_type type;

	for (type = PIDTYPE_PID; type < PIDTYPE_MAX; ++type)
		INIT_HLIST_NODE(&task->pid_links[type]);
}

static inline void
init_task_pid(struct task_struct *task, enum pid_type type, struct pid *pid)
{
	if (type == PIDTYPE_PID)
		task->thread_pid = pid;
	else
		task->signal->pids[type] = pid;
}

static inline void rcu_copy_process(struct task_struct *p)
{
}

struct pid *pidfd_pid(const struct file *file)
{
	if (file->f_op == &pidfd_fops)
		return file->private_data;

	return ERR_PTR(-EBADF);
}

static int pidfd_release(struct inode *inode, struct file *file)
{
	struct pid *pid = file->private_data;

	file->private_data = NULL;
	put_pid(pid);
	return 0;
}

static __poll_t pidfd_poll(struct file *file, struct poll_table_struct *pts)
{
	struct pid *pid = file->private_data;
	__poll_t poll_flags = 0;

	poll_wait(file, &pid->wait_pidfd, pts);

	
	if (thread_group_exited(pid))
		poll_flags = EPOLLIN | EPOLLRDNORM;

	return poll_flags;
}

const struct file_operations pidfd_fops = {
	.release = pidfd_release,
	.poll = pidfd_poll,
};

static void __delayed_free_task(struct rcu_head *rhp)
{
	struct task_struct *tsk = container_of(rhp, struct task_struct, rcu);

	free_task(tsk);
}

static __always_inline void delayed_free_task(struct task_struct *tsk)
{
	if (IS_ENABLED(CONFIG_MEMCG))
		call_rcu(&tsk->rcu, __delayed_free_task);
	else
		free_task(tsk);
}

static void copy_oom_score_adj(u64 clone_flags, struct task_struct *tsk)
{
	
	if (!tsk->mm)
		return;

	
	if ((clone_flags & (CLONE_VM | CLONE_THREAD | CLONE_VFORK)) != CLONE_VM)
		return;

	
	mutex_lock(&oom_adj_mutex);
	set_bit(MMF_MULTIPROCESS, &tsk->mm->flags);
	
	tsk->signal->oom_score_adj = current->signal->oom_score_adj;
	tsk->signal->oom_score_adj_min = current->signal->oom_score_adj_min;
	mutex_unlock(&oom_adj_mutex);
}

static __latent_entropy struct task_struct *copy_process(
					struct pid *pid,
					int trace,
					int node,
					struct kernel_clone_args *args)
{
	int pidfd = -1, retval;
	struct task_struct *p;
	struct multiprocess_signals delayed;
	struct file *pidfile = NULL;
	const u64 clone_flags = args->flags;
	struct nsproxy *nsp = current->nsproxy;

	
	if ((clone_flags & (CLONE_NEWNS|CLONE_FS)) == (CLONE_NEWNS|CLONE_FS))
		return ERR_PTR(-EINVAL);

	if ((clone_flags & (CLONE_NEWUSER|CLONE_FS)) == (CLONE_NEWUSER|CLONE_FS))
		return ERR_PTR(-EINVAL);

	
	if ((clone_flags & CLONE_THREAD) && !(clone_flags & CLONE_SIGHAND))
		return ERR_PTR(-EINVAL);

	
	if ((clone_flags & CLONE_SIGHAND) && !(clone_flags & CLONE_VM))
		return ERR_PTR(-EINVAL);

	
	if ((clone_flags & CLONE_PARENT) &&
				current->signal->flags & SIGNAL_UNKILLABLE)
		return ERR_PTR(-EINVAL);

	
	if (clone_flags & CLONE_THREAD) {
		if ((clone_flags & (CLONE_NEWUSER | CLONE_NEWPID)) ||
		    (task_active_pid_ns(current) != nsp->pid_ns_for_children))
			return ERR_PTR(-EINVAL);
	}

	
	if (clone_flags & (CLONE_THREAD | CLONE_VM)) {
		if (nsp->time_ns != nsp->time_ns_for_children)
			return ERR_PTR(-EINVAL);
	}

	if (clone_flags & CLONE_PIDFD) {
		
		if (clone_flags & (CLONE_DETACHED | CLONE_THREAD))
			return ERR_PTR(-EINVAL);
	}

	
	sigemptyset(&delayed.signal);
	INIT_HLIST_NODE(&delayed.node);

	spin_lock_irq(&current->sighand->siglock);
	if (!(clone_flags & CLONE_THREAD))
		hlist_add_head(&delayed.node, &current->signal->multiprocess);
	recalc_sigpending();
	spin_unlock_irq(&current->sighand->siglock);
	retval = -ERESTARTNOINTR;
	if (task_sigpending(current))
		goto fork_out;

	retval = -ENOMEM;
	p = dup_task_struct(current, node);
	if (!p)
		goto fork_out;
	p->flags &= ~PF_KTHREAD;
	if (args->kthread)
		p->flags |= PF_KTHREAD;
	if (args->io_thread) {
		
		p->flags |= PF_IO_WORKER;
		siginitsetinv(&p->blocked, sigmask(SIGKILL)|sigmask(SIGSTOP));
	}

	p->set_child_tid = (clone_flags & CLONE_CHILD_SETTID) ? args->child_tid : NULL;

	p->clear_child_tid = (clone_flags & CLONE_CHILD_CLEARTID) ? args->child_tid : NULL;

	rt_mutex_init_task(p);

	lockdep_assert_irqs_enabled();
	retval = copy_creds(p, clone_flags);
	if (retval < 0)
		goto bad_fork_free;

	retval = -EAGAIN;
	if (is_ucounts_overlimit(task_ucounts(p), UCOUNT_RLIMIT_NPROC, rlimit(RLIMIT_NPROC))) {
		if (p->real_cred->user != INIT_USER &&
		    !capable(CAP_SYS_RESOURCE) && !capable(CAP_SYS_ADMIN))
			goto bad_fork_cleanup_count;
	}
	current->flags &= ~PF_NPROC_EXCEEDED;

	
	retval = -EAGAIN;
	if (data_race(nr_threads >= max_threads))
		goto bad_fork_cleanup_count;

	delayacct_tsk_init(p);	
	p->flags &= ~(PF_SUPERPRIV | PF_WQ_WORKER | PF_IDLE | PF_NO_SETAFFINITY);
	p->flags |= PF_FORKNOEXEC;
	INIT_LIST_HEAD(&p->children);
	INIT_LIST_HEAD(&p->sibling);
	rcu_copy_process(p);
	p->vfork_done = NULL;
	spin_lock_init(&p->alloc_lock);

	init_sigpending(&p->pending);

	p->utime = p->stime = p->gtime = 0;
	prev_cputime_init(&p->prev_cputime);

#if defined(SPLIT_RSS_COUNTING)
	memset(&p->rss_stat, 0, sizeof(p->rss_stat));
#endif

	p->default_timer_slack_ns = current->timer_slack_ns;

	task_io_accounting_init(&p->ioac);
	acct_clear_integrals(p);

	posix_cputimers_init(&p->posix_cputimers);

	p->io_context = NULL;
	audit_set_context(p, NULL);
	cgroup_fork(p);
	if (args->kthread) {
		if (!set_kthread_struct(p))
			goto bad_fork_cleanup_delayacct;
	}

	p->pagefault_disabled = 0;

	
	retval = sched_fork(clone_flags, p);
	if (retval)
		goto bad_fork_cleanup_policy;

	retval = perf_event_init_task(p, clone_flags);
	if (retval)
		goto bad_fork_cleanup_policy;
	retval = audit_alloc(p);
	if (retval)
		goto bad_fork_cleanup_perf;
	
	shm_init_task(p);
	retval = security_task_alloc(p, clone_flags);
	if (retval)
		goto bad_fork_cleanup_audit;
	retval = copy_semundo(clone_flags, p);
	if (retval)
		goto bad_fork_cleanup_security;
	retval = copy_files(clone_flags, p);
	if (retval)
		goto bad_fork_cleanup_semundo;
	retval = copy_fs(clone_flags, p);
	if (retval)
		goto bad_fork_cleanup_files;
	retval = copy_sighand(clone_flags, p);
	if (retval)
		goto bad_fork_cleanup_fs;
	retval = copy_signal(clone_flags, p);
	if (retval)
		goto bad_fork_cleanup_sighand;
	retval = copy_mm(clone_flags, p);
	if (retval)
		goto bad_fork_cleanup_signal;
	retval = copy_namespaces(clone_flags, p);
	if (retval)
		goto bad_fork_cleanup_mm;
	retval = copy_io(clone_flags, p);
	if (retval)
		goto bad_fork_cleanup_namespaces;
	retval = copy_thread(p, args);
	if (retval)
		goto bad_fork_cleanup_io;

	if (pid != &init_struct_pid) {
		pid = alloc_pid(p->nsproxy->pid_ns_for_children, args->set_tid,
				args->set_tid_size);
		if (IS_ERR(pid)) {
			retval = PTR_ERR(pid);
			goto bad_fork_cleanup_thread;
		}
	}

	
	if (clone_flags & CLONE_PIDFD) {
		retval = get_unused_fd_flags(O_RDWR | O_CLOEXEC);
		if (retval < 0)
			goto bad_fork_free_pid;

		pidfd = retval;

		pidfile = anon_inode_getfile("[pidfd]", &pidfd_fops, pid,
					      O_RDWR | O_CLOEXEC);
		if (IS_ERR(pidfile)) {
			put_unused_fd(pidfd);
			retval = PTR_ERR(pidfile);
			goto bad_fork_free_pid;
		}
		get_pid(pid);	

		retval = put_user(pidfd, args->pidfd);
		if (retval)
			goto bad_fork_put_pidfd;
	}

	futex_init_task(p);

	
	if ((clone_flags & (CLONE_VM|CLONE_VFORK)) == CLONE_VM)
		sas_ss_reset(p);

	
	user_disable_single_step(p);
	clear_task_syscall_work(p, SYSCALL_TRACE);
	clear_task_syscall_work(p, SYSCALL_EMU);
	clear_tsk_latency_tracing(p);

	
	p->pid = pid_nr(pid);
	if (clone_flags & CLONE_THREAD) {
		p->group_leader = current->group_leader;
		p->tgid = current->tgid;
	} else {
		p->group_leader = p;
		p->tgid = p->pid;
	}

	p->nr_dirtied = 0;
	p->nr_dirtied_pause = 128 >> (PAGE_SHIFT - 10);
	p->dirty_paused_when = 0;

	p->pdeath_signal = 0;
	INIT_LIST_HEAD(&p->thread_group);
	p->task_works = NULL;
	clear_posix_cputimers_work(p);

	
	retval = cgroup_can_fork(p, args);
	if (retval)
		goto bad_fork_put_pidfd;

	
	sched_cgroup_fork(p, args);

	

	p->start_time = ktime_get_ns();
	p->start_boottime = ktime_get_boottime_ns();

	
	write_lock_irq(&tasklist_lock);

	
	if (clone_flags & (CLONE_PARENT|CLONE_THREAD)) {
		p->real_parent = current->real_parent;
		p->parent_exec_id = current->parent_exec_id;
		if (clone_flags & CLONE_THREAD)
			p->exit_signal = -1;
		else
			p->exit_signal = current->group_leader->exit_signal;
	} else {
		p->real_parent = current;
		p->parent_exec_id = current->self_exec_id;
		p->exit_signal = args->exit_signal;
	}

	klp_copy_process(p);

	sched_core_fork(p);

	spin_lock(&current->sighand->siglock);

	
	copy_seccomp(p);

	rseq_fork(p, clone_flags);

	
	if (unlikely(!(ns_of_pid(pid)->pid_allocated & PIDNS_ADDING))) {
		retval = -ENOMEM;
		goto bad_fork_cancel_cgroup;
	}

	
	if (fatal_signal_pending(current)) {
		retval = -EINTR;
		goto bad_fork_cancel_cgroup;
	}

	init_task_pid_links(p);
	if (likely(p->pid)) {
		ptrace_init_task(p, (clone_flags & CLONE_PTRACE) || trace);

		init_task_pid(p, PIDTYPE_PID, pid);
		if (thread_group_leader(p)) {
			init_task_pid(p, PIDTYPE_TGID, pid);
			init_task_pid(p, PIDTYPE_PGID, task_pgrp(current));
			init_task_pid(p, PIDTYPE_SID, task_session(current));

			if (is_child_reaper(pid)) {
				ns_of_pid(pid)->child_reaper = p;
				p->signal->flags |= SIGNAL_UNKILLABLE;
			}
			p->signal->shared_pending.signal = delayed.signal;
			p->signal->tty = tty_kref_get(current->signal->tty);
			
			p->signal->has_child_subreaper = p->real_parent->signal->has_child_subreaper ||
							 p->real_parent->signal->is_child_subreaper;
			list_add_tail(&p->sibling, &p->real_parent->children);
			list_add_tail_rcu(&p->tasks, &init_task.tasks);
			attach_pid(p, PIDTYPE_TGID);
			attach_pid(p, PIDTYPE_PGID);
			attach_pid(p, PIDTYPE_SID);
			__this_cpu_inc(process_counts);
		} else {
			current->signal->nr_threads++;
			atomic_inc(&current->signal->live);
			refcount_inc(&current->signal->sigcnt);
			task_join_group_stop(p);
			list_add_tail_rcu(&p->thread_group,
					  &p->group_leader->thread_group);
			list_add_tail_rcu(&p->thread_node,
					  &p->signal->thread_head);
		}
		attach_pid(p, PIDTYPE_PID);
		nr_threads++;
	}
	total_forks++;
	hlist_del_init(&delayed.node);
	spin_unlock(&current->sighand->siglock);
	syscall_tracepoint_update(p);
	write_unlock_irq(&tasklist_lock);

	if (pidfile)
		fd_install(pidfd, pidfile);

	proc_fork_connector(p);
	sched_post_fork(p);
	cgroup_post_fork(p, args);
	perf_event_fork(p);

	
	uprobe_copy_process(p, clone_flags);

	copy_oom_score_adj(clone_flags, p);

	return p;

bad_fork_cancel_cgroup:
	sched_core_free(p);
	spin_unlock(&current->sighand->siglock);
	write_unlock_irq(&tasklist_lock);
	cgroup_cancel_fork(p, args);
bad_fork_put_pidfd:
	if (clone_flags & CLONE_PIDFD) {
		fput(pidfile);
		put_unused_fd(pidfd);
	}
bad_fork_free_pid:
	if (pid != &init_struct_pid)
		free_pid(pid);
bad_fork_cleanup_thread:
	exit_thread(p);
bad_fork_cleanup_io:
	if (p->io_context)
		exit_io_context(p);
bad_fork_cleanup_namespaces:
	exit_task_namespaces(p);
bad_fork_cleanup_mm:
	if (p->mm) {
		mm_clear_owner(p->mm, p);
		mmput(p->mm);
	}
bad_fork_cleanup_signal:
	if (!(clone_flags & CLONE_THREAD))
		free_signal_struct(p->signal);
bad_fork_cleanup_sighand:
	__cleanup_sighand(p->sighand);
bad_fork_cleanup_fs:
	exit_fs(p); 
bad_fork_cleanup_files:
	exit_files(p); 
bad_fork_cleanup_semundo:
	exit_sem(p);
bad_fork_cleanup_security:
	security_task_free(p);
bad_fork_cleanup_audit:
	audit_free(p);
bad_fork_cleanup_perf:
	perf_event_free_task(p);
bad_fork_cleanup_policy:
	lockdep_free_task(p);
bad_fork_cleanup_delayacct:
	delayacct_tsk_free(p);
bad_fork_cleanup_count:
	dec_rlimit_ucounts(task_ucounts(p), UCOUNT_RLIMIT_NPROC, 1);
	exit_creds(p);
bad_fork_free:
	WRITE_ONCE(p->__state, TASK_DEAD);
	exit_task_stack_account(p);
	put_task_stack(p);
	delayed_free_task(p);
fork_out:
	spin_lock_irq(&current->sighand->siglock);
	hlist_del_init(&delayed.node);
	spin_unlock_irq(&current->sighand->siglock);
	return ERR_PTR(retval);
}

static inline void init_idle_pids(struct task_struct *idle)
{
	enum pid_type type;

	for (type = PIDTYPE_PID; type < PIDTYPE_MAX; ++type) {
		INIT_HLIST_NODE(&idle->pid_links[type]); 
		init_task_pid(idle, type, &init_struct_pid);
	}
}

static int idle_dummy(void *dummy)
{
	
	return 0;
}

struct task_struct * __init fork_idle(int cpu)
{
	struct task_struct *task;
	struct kernel_clone_args args = {
		.flags		= CLONE_VM,
		.fn		= &idle_dummy,
		.fn_arg		= NULL,
		.kthread	= 1,
		.idle		= 1,
	};

	task = copy_process(&init_struct_pid, 0, cpu_to_node(cpu), &args);
	if (!IS_ERR(task)) {
		init_idle_pids(task);
		init_idle(task, cpu);
	}

	return task;
}

struct mm_struct *copy_init_mm(void)
{
	return dup_mm(NULL, &init_mm);
}

struct task_struct *create_io_thread(int (*fn)(void *), void *arg, int node)
{
	unsigned long flags = CLONE_FS|CLONE_FILES|CLONE_SIGHAND|CLONE_THREAD|
				CLONE_IO;
	struct kernel_clone_args args = {
		.flags		= ((lower_32_bits(flags) | CLONE_VM |
				    CLONE_UNTRACED) & ~CSIGNAL),
		.exit_signal	= (lower_32_bits(flags) & CSIGNAL),
		.fn		= fn,
		.fn_arg		= arg,
		.io_thread	= 1,
	};

	return copy_process(NULL, 0, node, &args);
}

pid_t kernel_clone(struct kernel_clone_args *args)
{
	u64 clone_flags = args->flags;
	struct completion vfork;
	struct pid *pid;
	struct task_struct *p;
	int trace = 0;
	pid_t nr;

	
	if ((args->flags & CLONE_PIDFD) &&
	    (args->flags & CLONE_PARENT_SETTID) &&
	    (args->pidfd == args->parent_tid))
		return -EINVAL;

	
	if (!(clone_flags & CLONE_UNTRACED)) {
		if (clone_flags & CLONE_VFORK)
			trace = PTRACE_EVENT_VFORK;
		else if (args->exit_signal != SIGCHLD)
			trace = PTRACE_EVENT_CLONE;
		else
			trace = PTRACE_EVENT_FORK;

		if (likely(!ptrace_event_enabled(current, trace)))
			trace = 0;
	}

	p = copy_process(NULL, trace, NUMA_NO_NODE, args);
	add_latent_entropy();

	if (IS_ERR(p))
		return PTR_ERR(p);

	
	

	pid = get_task_pid(p, PIDTYPE_PID);
	nr = pid_vnr(pid);

	if (clone_flags & CLONE_PARENT_SETTID)
		put_user(nr, args->parent_tid);

	if (clone_flags & CLONE_VFORK) {
		p->vfork_done = &vfork;
		init_completion(&vfork);
		get_task_struct(p);
	}

	wake_up_new_task(p);

	
	if (unlikely(trace))
		ptrace_event_pid(trace, pid);

	if (clone_flags & CLONE_VFORK) {
		if (!wait_for_vfork_done(p, &vfork))
			ptrace_event_pid(PTRACE_EVENT_VFORK_DONE, pid);
	}

	put_pid(pid);
	return nr;
}

pid_t kernel_thread(int (*fn)(void *), void *arg, unsigned long flags)
{
	struct kernel_clone_args args = {
		.flags		= ((lower_32_bits(flags) | CLONE_VM |
				    CLONE_UNTRACED) & ~CSIGNAL),
		.exit_signal	= (lower_32_bits(flags) & CSIGNAL),
		.fn		= fn,
		.fn_arg		= arg,
		.kthread	= 1,
	};

	return kernel_clone(&args);
}

pid_t user_mode_thread(int (*fn)(void *), void *arg, unsigned long flags)
{
	struct kernel_clone_args args = {
		.flags		= ((lower_32_bits(flags) | CLONE_VM |
				    CLONE_UNTRACED) & ~CSIGNAL),
		.exit_signal	= (lower_32_bits(flags) & CSIGNAL),
		.fn		= fn,
		.fn_arg		= arg,
	};

	return kernel_clone(&args);
}

#ifdef __ARCH_WANT_SYS_FORK
SYSCALL_DEFINE0(fork)
{
	struct kernel_clone_args args = {
		.exit_signal = SIGCHLD,
	};

	return kernel_clone(&args);
}
#endif

#ifdef __ARCH_WANT_SYS_VFORK
SYSCALL_DEFINE0(vfork)
{
	struct kernel_clone_args args = {
		.flags		= CLONE_VFORK | CLONE_VM,
		.exit_signal	= SIGCHLD,
	};

	return kernel_clone(&args);
}
#endif

#ifdef __ARCH_WANT_SYS_CLONE
SYSCALL_DEFINE5(clone, unsigned long, clone_flags, unsigned long, newsp,
		 int __user *, parent_tidptr,
		 unsigned long, tls,
		 int __user *, child_tidptr)
{
	struct kernel_clone_args args = {
		.flags		= (lower_32_bits(clone_flags) & ~CSIGNAL),
		.pidfd		= parent_tidptr,
		.child_tid	= child_tidptr,
		.parent_tid	= parent_tidptr,
		.exit_signal	= (lower_32_bits(clone_flags) & CSIGNAL),
		.stack		= newsp,
		.tls		= tls,
	};

	return kernel_clone(&args);
}
#endif

#ifdef __ARCH_WANT_SYS_CLONE3

noinline static int copy_clone_args_from_user(struct kernel_clone_args *kargs,
					      struct clone_args __user *uargs,
					      size_t usize)
{
	int err;
	struct clone_args args;
	pid_t *kset_tid = kargs->set_tid;

	BUILD_BUG_ON(offsetofend(struct clone_args, tls) !=
		     CLONE_ARGS_SIZE_VER0);
	BUILD_BUG_ON(offsetofend(struct clone_args, set_tid_size) !=
		     CLONE_ARGS_SIZE_VER1);
	BUILD_BUG_ON(offsetofend(struct clone_args, cgroup) !=
		     CLONE_ARGS_SIZE_VER2);
	BUILD_BUG_ON(sizeof(struct clone_args) != CLONE_ARGS_SIZE_VER2);

	if (unlikely(usize > PAGE_SIZE))
		return -E2BIG;
	if (unlikely(usize < CLONE_ARGS_SIZE_VER0))
		return -EINVAL;

	err = copy_struct_from_user(&args, sizeof(args), uargs, usize);
	if (err)
		return err;

	if (unlikely(args.set_tid_size > MAX_PID_NS_LEVEL))
		return -EINVAL;

	if (unlikely(!args.set_tid && args.set_tid_size > 0))
		return -EINVAL;

	if (unlikely(args.set_tid && args.set_tid_size == 0))
		return -EINVAL;

	
	if (unlikely((args.exit_signal & ~((u64)CSIGNAL)) ||
		     !valid_signal(args.exit_signal)))
		return -EINVAL;

	if ((args.flags & CLONE_INTO_CGROUP) &&
	    (args.cgroup > INT_MAX || usize < CLONE_ARGS_SIZE_VER2))
		return -EINVAL;

	*kargs = (struct kernel_clone_args){
		.flags		= args.flags,
		.pidfd		= u64_to_user_ptr(args.pidfd),
		.child_tid	= u64_to_user_ptr(args.child_tid),
		.parent_tid	= u64_to_user_ptr(args.parent_tid),
		.exit_signal	= args.exit_signal,
		.stack		= args.stack,
		.stack_size	= args.stack_size,
		.tls		= args.tls,
		.set_tid_size	= args.set_tid_size,
		.cgroup		= args.cgroup,
	};

	if (args.set_tid &&
		copy_from_user(kset_tid, u64_to_user_ptr(args.set_tid),
			(kargs->set_tid_size * sizeof(pid_t))))
		return -EFAULT;

	kargs->set_tid = kset_tid;

	return 0;
}

static inline bool clone3_stack_valid(struct kernel_clone_args *kargs)
{
	if (kargs->stack == 0) {
		if (kargs->stack_size > 0)
			return false;
	} else {
		if (kargs->stack_size == 0)
			return false;

		if (!access_ok((void __user *)kargs->stack, kargs->stack_size))
			return false;

		kargs->stack += kargs->stack_size;
	}

	return true;
}

static bool clone3_args_valid(struct kernel_clone_args *kargs)
{
	
	if (kargs->flags &
	    ~(CLONE_LEGACY_FLAGS | CLONE_CLEAR_SIGHAND | CLONE_INTO_CGROUP))
		return false;

	
	if (kargs->flags & (CLONE_DETACHED | CSIGNAL))
		return false;

	if ((kargs->flags & (CLONE_SIGHAND | CLONE_CLEAR_SIGHAND)) ==
	    (CLONE_SIGHAND | CLONE_CLEAR_SIGHAND))
		return false;

	if ((kargs->flags & (CLONE_THREAD | CLONE_PARENT)) &&
	    kargs->exit_signal)
		return false;

	if (!clone3_stack_valid(kargs))
		return false;

	return true;
}

SYSCALL_DEFINE2(clone3, struct clone_args __user *, uargs, size_t, size)
{
	int err;

	struct kernel_clone_args kargs;
	pid_t set_tid[MAX_PID_NS_LEVEL];

	kargs.set_tid = set_tid;

	err = copy_clone_args_from_user(&kargs, uargs, size);
	if (err)
		return err;

	if (!clone3_args_valid(&kargs))
		return -EINVAL;

	return kernel_clone(&kargs);
}
#endif

void walk_process_tree(struct task_struct *top, proc_visitor visitor, void *data)
{
	struct task_struct *leader, *parent, *child;
	int res;

	read_lock(&tasklist_lock);
	leader = top = top->group_leader;
down:
	for_each_thread(leader, parent) {
		list_for_each_entry(child, &parent->children, sibling) {
			res = visitor(child, data);
			if (res) {
				if (res < 0)
					goto out;
				leader = child;
				goto down;
			}
up:
			;
		}
	}

	if (leader != top) {
		child = leader;
		parent = child->real_parent;
		leader = parent->group_leader;
		goto up;
	}
out:
	read_unlock(&tasklist_lock);
}

#ifndef ARCH_MIN_MMSTRUCT_ALIGN
#define ARCH_MIN_MMSTRUCT_ALIGN 0
#endif

static void sighand_ctor(void *data)
{
	struct sighand_struct *sighand = data;

	spin_lock_init(&sighand->siglock);
	init_waitqueue_head(&sighand->signalfd_wqh);
}

void __init proc_caches_init(void)
{
	unsigned int mm_size;

	sighand_cachep = kmem_cache_create("sighand_cache",
			sizeof(struct sighand_struct), 0,
			SLAB_HWCACHE_ALIGN|SLAB_PANIC|SLAB_TYPESAFE_BY_RCU|
			SLAB_ACCOUNT, sighand_ctor);
	signal_cachep = kmem_cache_create("signal_cache",
			sizeof(struct signal_struct), 0,
			SLAB_HWCACHE_ALIGN|SLAB_PANIC|SLAB_ACCOUNT,
			NULL);
	files_cachep = kmem_cache_create("files_cache",
			sizeof(struct files_struct), 0,
			SLAB_HWCACHE_ALIGN|SLAB_PANIC|SLAB_ACCOUNT,
			NULL);
	fs_cachep = kmem_cache_create("fs_cache",
			sizeof(struct fs_struct), 0,
			SLAB_HWCACHE_ALIGN|SLAB_PANIC|SLAB_ACCOUNT,
			NULL);

	
	mm_size = sizeof(struct mm_struct) + cpumask_size();

	mm_cachep = kmem_cache_create_usercopy("mm_struct",
			mm_size, ARCH_MIN_MMSTRUCT_ALIGN,
			SLAB_HWCACHE_ALIGN|SLAB_PANIC|SLAB_ACCOUNT,
			offsetof(struct mm_struct, saved_auxv),
			sizeof_field(struct mm_struct, saved_auxv),
			NULL);
	vm_area_cachep = KMEM_CACHE(vm_area_struct, SLAB_PANIC|SLAB_ACCOUNT);
	mmap_init();
	nsproxy_cache_init();
}

static int check_unshare_flags(unsigned long unshare_flags)
{
	
	return 0;
}

static int unshare_fs(unsigned long unshare_flags, struct fs_struct **new_fsp)
{
	struct fs_struct *fs = current->fs;

	if (!(unshare_flags & CLONE_FS) || !fs)
		return 0;

	
	if (fs->users == 1)
		return 0;

	*new_fsp = copy_fs_struct(fs);
	if (!*new_fsp)
		return -ENOMEM;

	return 0;
}

int unshare_fd(unsigned long unshare_flags, unsigned int max_fds,
	       struct files_struct **new_fdp)
{
	struct files_struct *fd = current->files;
	int error = 0;

	if ((unshare_flags & CLONE_FILES) &&
	    (fd && atomic_read(&fd->count) > 1)) {
		*new_fdp = dup_fd(fd, max_fds, &error);
		if (!*new_fdp)
			return error;
	}

	return 0;
}

int ksys_unshare(unsigned long unshare_flags)
{
	/* Stubbed: namespace unsharing not needed for minimal boot */
	return -EINVAL;
}

SYSCALL_DEFINE1(unshare, unsigned long, unshare_flags)
{
	return ksys_unshare(unshare_flags);
}

int unshare_files(void)
{
	struct task_struct *task = current;
	struct files_struct *old, *copy = NULL;
	int error;

	error = unshare_fd(CLONE_FILES, NR_OPEN_MAX, &copy);
	if (error || !copy)
		return error;

	old = task->files;
	task_lock(task);
	task->files = copy;
	task_unlock(task);
	put_files_struct(old);
	return 0;
}

int sysctl_max_threads(struct ctl_table *table, int write,
		       void *buffer, size_t *lenp, loff_t *ppos)
{
	struct ctl_table t;
	int ret;
	int threads = max_threads;
	int min = 1;
	int max = MAX_THREADS;

	t = *table;
	t.data = &threads;
	t.extra1 = &min;
	t.extra2 = &max;

	ret = proc_dointvec_minmax(&t, write, buffer, lenp, ppos);
	if (ret || !write)
		return ret;

	max_threads = threads;

	return 0;
}
