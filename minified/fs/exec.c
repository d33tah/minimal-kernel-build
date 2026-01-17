
#include <linux/slab.h>
#include <linux/file.h>
#include <linux/fdtable.h>
#include <linux/mm.h>
#include <linux/vmacache.h>
#include <linux/stat.h>
#include <linux/swap.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/sched/mm.h>
#include <linux/sched/coredump.h>
#include <linux/sched/signal.h>
#include <linux/sched/task.h>
#include <linux/pagemap.h>
#include <linux/highmem.h>
#include <linux/spinlock.h>
#include <linux/personality.h>
#include <linux/binfmts.h>
#include <linux/pid_namespace.h>
#include <linux/module.h>
#include <linux/namei.h>
#include <linux/mount.h>
#include <linux/security.h>
#include <linux/syscalls.h>
#include <linux/fs_struct.h>
#include <linux/vmalloc.h>
#include <linux/syscall_user_dispatch.h>
#include <linux/ptrace.h>

#include <linux/uaccess.h>
#include <asm/mmu_context.h>
#include <asm/tlb.h>

#include "internal.h"

static void bprm_creds_from_file(struct linux_binprm *bprm);

int suid_dumpable = 0;

static LIST_HEAD(formats);
static DEFINE_RWLOCK(binfmt_lock);

void __register_binfmt(struct linux_binfmt *fmt, int insert)
{
	write_lock(&binfmt_lock);
	insert ? list_add(&fmt->lh, &formats) :
		 list_add_tail(&fmt->lh, &formats);
	write_unlock(&binfmt_lock);
}

bool path_noexec(const struct path *path)
{
	return (path->mnt->mnt_flags & MNT_NOEXEC) ||
	       (path->mnt->mnt_sb->s_iflags & SB_I_NOEXEC);
}

static void acct_arg_size(struct linux_binprm *bprm, unsigned long pages)
{
	struct mm_struct *mm = current->mm;
	long diff = (long)(pages - bprm->vma_pages);

	if (!mm || !diff)
		return;

	bprm->vma_pages = pages;
	add_mm_counter(mm, MM_ANONPAGES, diff);
}

static struct page *get_arg_page(struct linux_binprm *bprm, unsigned long pos,
				 int write)
{
	struct page *page;
	int ret;
	unsigned int gup_flags = FOLL_FORCE;

	if (write)
		gup_flags |= FOLL_WRITE;

	mmap_read_lock(bprm->mm);
	ret = get_user_pages_remote(bprm->mm, pos, 1, gup_flags, &page, NULL,
				    NULL);
	mmap_read_unlock(bprm->mm);
	if (ret <= 0)
		return NULL;

	if (write)
		acct_arg_size(bprm, vma_pages(bprm->vma));

	return page;
}

/* put_arg_page and flush_arg_page inlined - trivial stubs */

static int bprm_mm_init(struct linux_binprm *bprm)
{
	int err;
	struct vm_area_struct *vma = NULL;
	struct mm_struct *mm = NULL;

	bprm->mm = mm = mm_alloc();
	if (!mm)
		return -ENOMEM;

	task_lock(current->group_leader);
	bprm->rlim_stack = current->signal->rlim[RLIMIT_STACK];
	task_unlock(current->group_leader);

	bprm->vma = vma = vm_area_alloc(mm);
	if (!vma)
		goto err_mm;
	vma_set_anonymous(vma);

	if (mmap_write_lock_killable(mm)) {
		err = -EINTR;
		goto err_vma;
	}

	BUILD_BUG_ON(VM_STACK_FLAGS & VM_STACK_INCOMPLETE_SETUP);
	vma->vm_end = STACK_TOP_MAX;
	vma->vm_start = vma->vm_end - PAGE_SIZE;
	vma->vm_flags = VM_SOFTDIRTY | VM_STACK_FLAGS |
			VM_STACK_INCOMPLETE_SETUP;
	vma->vm_page_prot = vm_get_page_prot(vma->vm_flags);

	err = insert_vm_struct(mm, vma);
	if (err)
		goto err_unlock;

	mm->total_vm = 1;
	mmap_write_unlock(mm);
	bprm->p = vma->vm_end - sizeof(void *);
	return 0;

err_unlock:
	mmap_write_unlock(mm);
err_vma:
	bprm->vma = NULL;
	vm_area_free(vma);
err_mm:
	bprm->mm = NULL;
	mmdrop(mm);
	return err;
}

/* Removed: user_arg_ptr, get_user_arg_ptr, count - only used by removed do_execveat_common */

static int count_strings_kernel(const char *const *argv)
{
	int i;

	if (!argv)
		return 0;

	for (i = 0; argv[i]; ++i) {
		if (i >= MAX_ARG_STRINGS)
			return -E2BIG;
		if (fatal_signal_pending(current))
			return -ERESTARTNOHAND;
		cond_resched();
	}
	return i;
}

/* Removed: copy_strings() - only used by removed do_execveat_common */

int copy_string_kernel(const char *arg, struct linux_binprm *bprm)
{
	int len = strnlen(arg, MAX_ARG_STRLEN) + 1;
	unsigned long pos = bprm->p;

	if (len == 0)
		return -EFAULT;
	if (len > MAX_ARG_STRLEN)
		return -E2BIG;

	arg += len;
	bprm->p -= len;
	/* CONFIG_MMU=y, so IS_ENABLED is always true */
	if (bprm->p < bprm->argmin)
		return -E2BIG;

	while (len > 0) {
		unsigned int bytes_to_copy =
			min_t(unsigned int, len,
			      min_not_zero(offset_in_page(pos), PAGE_SIZE));
		struct page *page;
		char *kaddr;

		pos -= bytes_to_copy;
		arg -= bytes_to_copy;
		len -= bytes_to_copy;

		page = get_arg_page(bprm, pos, 1);
		if (!page)
			return -E2BIG;
		kaddr = kmap_atomic(page);
		memcpy(kaddr + offset_in_page(pos), arg, bytes_to_copy);
		kunmap_atomic(kaddr);
		put_page(page);
	}

	return 0;
}

static int copy_strings_kernel(int argc, const char *const *argv,
			       struct linux_binprm *bprm)
{
	while (argc-- > 0) {
		int ret = copy_string_kernel(argv[argc], bprm);
		if (ret < 0)
			return ret;
		if (fatal_signal_pending(current))
			return -ERESTARTNOHAND;
		cond_resched();
	}
	return 0;
}

static int shift_arg_pages(struct vm_area_struct *vma, unsigned long shift)
{
	struct mm_struct *mm = vma->vm_mm;
	unsigned long old_start = vma->vm_start;
	unsigned long old_end = vma->vm_end;
	unsigned long length = old_end - old_start;
	unsigned long new_start = old_start - shift;
	unsigned long new_end = old_end - shift;
	struct mmu_gather tlb;

	BUG_ON(new_start > new_end);

	if (vma != find_vma(mm, new_start))
		return -EFAULT;

	if (vma_adjust(vma, new_start, old_end, vma->vm_pgoff, NULL))
		return -ENOMEM;

	if (length !=
	    move_page_tables(vma, old_start, vma, new_start, length, false))
		return -ENOMEM;

	lru_add_drain();
	tlb_gather_mmu(&tlb, mm);
	if (new_end > old_start) {
		free_pgd_range(&tlb, new_end, old_end, new_end,
			       vma->vm_next ? vma->vm_next->vm_start :
					      USER_PGTABLES_CEILING);
	} else {
		free_pgd_range(&tlb, old_start, old_end, new_end,
			       vma->vm_next ? vma->vm_next->vm_start :
					      USER_PGTABLES_CEILING);
	}
	tlb_finish_mmu(&tlb);

	vma_adjust(vma, new_start, new_end, vma->vm_pgoff, NULL);

	return 0;
}

int setup_arg_pages(struct linux_binprm *bprm, unsigned long stack_top,
		    int executable_stack)
{
	unsigned long ret;
	unsigned long stack_shift;
	struct mm_struct *mm = current->mm;
	struct vm_area_struct *vma = bprm->vma;
	unsigned long vm_flags;
	unsigned long stack_base;
	unsigned long stack_size;
	unsigned long stack_expand;
	unsigned long rlim_stack;

	stack_top = arch_align_stack(stack_top);
	stack_top = PAGE_ALIGN(stack_top);

	if (unlikely(stack_top < mmap_min_addr) ||
	    unlikely(vma->vm_end - vma->vm_start >= stack_top - mmap_min_addr))
		return -ENOMEM;

	stack_shift = vma->vm_end - stack_top;

	bprm->p -= stack_shift;
	mm->arg_start = bprm->p;

	/* bprm->loader check removed - never set (kzalloc zeros) */
	bprm->exec -= stack_shift;

	if (mmap_write_lock_killable(mm))
		return -EINTR;

	vm_flags = VM_STACK_FLAGS;

	if (unlikely(executable_stack == EXSTACK_ENABLE_X))
		vm_flags |= VM_EXEC;
	else if (executable_stack == EXSTACK_DISABLE_X)
		vm_flags &= ~VM_EXEC;
	vm_flags |= mm->def_flags;
	vm_flags |= VM_STACK_INCOMPLETE_SETUP;
	/* mprotect_fixup removed - was a no-op stub */

	if (stack_shift) {
		ret = shift_arg_pages(vma, stack_shift);
		if (ret)
			goto out_unlock;
	}

	vma->vm_flags &= ~VM_STACK_INCOMPLETE_SETUP;

	stack_expand = 131072UL;
	stack_size = vma->vm_end - vma->vm_start;

	rlim_stack = bprm->rlim_stack.rlim_cur & PAGE_MASK;
	if (stack_size + stack_expand > rlim_stack)
		stack_base = vma->vm_end - rlim_stack;
	else
		stack_base = vma->vm_start - stack_expand;
	current->mm->start_stack = bprm->p;
	ret = expand_stack(vma, stack_base);
	if (ret)
		ret = -EFAULT;

out_unlock:
	mmap_write_unlock(mm);
	return ret;
}

static struct file *do_open_execat(int fd, struct filename *name, int flags)
{
	struct file *file;
	int err;
	struct open_flags open_exec_flags = {
		.open_flag = O_LARGEFILE | O_RDONLY | __FMODE_EXEC,
		.acc_mode = MAY_EXEC,
		.intent = LOOKUP_OPEN,
		.lookup_flags = LOOKUP_FOLLOW,
	};

	if ((flags & ~(AT_SYMLINK_NOFOLLOW | AT_EMPTY_PATH)) != 0)
		return ERR_PTR(-EINVAL);
	if (flags & AT_SYMLINK_NOFOLLOW)
		open_exec_flags.lookup_flags &= ~LOOKUP_FOLLOW;
	if (flags & AT_EMPTY_PATH)
		open_exec_flags.lookup_flags |= LOOKUP_EMPTY;

	file = do_filp_open(fd, name, &open_exec_flags);
	if (IS_ERR(file))
		goto out;

	err = -EACCES;
	if (WARN_ON_ONCE(!S_ISREG(file_inode(file)->i_mode) ||
			 path_noexec(&file->f_path)))
		goto exit;

	err = deny_write_access(file);
	if (err)
		goto exit;

out:
	return file;

exit:
	fput(file);
	return ERR_PTR(err);
}

struct file *open_exec(const char *name)
{
	struct filename *filename = getname_kernel(name);
	struct file *f = ERR_CAST(filename);

	if (!IS_ERR(filename)) {
		f = do_open_execat(AT_FDCWD, filename, 0);
		putname(filename);
	}
	return f;
}

static int exec_mmap(struct mm_struct *mm)
{
	struct task_struct *tsk;
	struct mm_struct *old_mm, *active_mm;
	int ret;

	tsk = current;
	old_mm = current->mm;
	exec_mm_release(tsk, old_mm);
	/* sync_mm_rss() - empty stub */
	ret = down_write_killable(&tsk->signal->exec_update_lock);
	if (ret)
		return ret;

	if (old_mm) {
		ret = mmap_read_lock_killable(old_mm);
		if (ret) {
			up_write(&tsk->signal->exec_update_lock);
			return ret;
		}
	}

	task_lock(tsk);
	/* membarrier_exec_mmap - empty stub removed */
	local_irq_disable();
	active_mm = tsk->active_mm;
	tsk->active_mm = mm;
	tsk->mm = mm;
	/* ARCH_WANT_IRQS_OFF_ACTIVATE_MM not defined */
	local_irq_enable();
	activate_mm(active_mm, mm);
	tsk->mm->vmacache_seqnum = 0;
	vmacache_flush(tsk);
	task_unlock(tsk);
	if (old_mm) {
		mmap_read_unlock(old_mm);
		BUG_ON(active_mm != old_mm);
		mmput(old_mm);
		return 0;
	}
	mmdrop(active_mm);
	return 0;
}

static int de_thread(struct task_struct *tsk)
{
	struct signal_struct *sig = tsk->signal;
	struct sighand_struct *oldsighand = tsk->sighand;
	spinlock_t *lock = &oldsighand->siglock;

	if (thread_group_empty(tsk))
		goto no_thread_group;

	spin_lock_irq(lock);
	if ((sig->flags & SIGNAL_GROUP_EXIT) || sig->group_exec_task) {
		spin_unlock_irq(lock);
		return -EAGAIN;
	}

	sig->group_exec_task = tsk;
	sig->notify_count = zap_other_threads(tsk);
	if (!thread_group_leader(tsk))
		sig->notify_count--;

	while (sig->notify_count) {
		__set_current_state(TASK_KILLABLE);
		spin_unlock_irq(lock);
		schedule();
		if (__fatal_signal_pending(tsk))
			goto killed;
		spin_lock_irq(lock);
	}
	spin_unlock_irq(lock);

	if (!thread_group_leader(tsk)) {
		struct task_struct *leader = tsk->group_leader;

		for (;;) {
			write_lock_irq(&tasklist_lock);

			sig->notify_count = -1;
			if (likely(leader->exit_state))
				break;
			__set_current_state(TASK_KILLABLE);
			write_unlock_irq(&tasklist_lock);
			schedule();
			if (__fatal_signal_pending(tsk))
				goto killed;
		}

		/* start_time removed - write-only field */

		BUG_ON(!same_thread_group(leader, tsk));

		exchange_tids(tsk, leader);
		transfer_pid(leader, tsk, PIDTYPE_TGID);
		transfer_pid(leader, tsk, PIDTYPE_PGID);
		transfer_pid(leader, tsk, PIDTYPE_SID);

		list_replace_rcu(&leader->tasks, &tsk->tasks);
		list_replace_init(&leader->sibling, &tsk->sibling);

		tsk->group_leader = tsk;
		leader->group_leader = tsk;

		tsk->exit_signal = SIGCHLD;
		leader->exit_signal = -1;

		BUG_ON(leader->exit_state != EXIT_ZOMBIE);
		leader->exit_state = EXIT_DEAD;
		write_unlock_irq(&tasklist_lock);

		release_task(leader);
	}

	sig->group_exec_task = NULL;
	sig->notify_count = 0;

no_thread_group:

	tsk->exit_signal = SIGCHLD;

	BUG_ON(!thread_group_leader(tsk));
	return 0;

killed:

	read_lock(&tasklist_lock);
	sig->group_exec_task = NULL;
	sig->notify_count = 0;
	read_unlock(&tasklist_lock);
	return -EAGAIN;
}

void __set_task_comm(struct task_struct *tsk, const char *buf, bool exec)
{
	task_lock(tsk);
	strscpy_pad(tsk->comm, buf, sizeof(tsk->comm));
	task_unlock(tsk);
}

int begin_new_exec(struct linux_binprm *bprm)
{
	struct task_struct *me = current;
	int retval;

	/* bprm_creds_from_file now returns void (security check removed) */
	bprm_creds_from_file(bprm);
	bprm->point_of_no_return = true;

	retval = de_thread(me);
	if (retval)
		goto out;
	/* io_uring_task_cancel() - empty stub */
	retval = unshare_files();
	if (retval)
		goto out;

	retval = set_mm_exe_file(bprm->mm, bprm->file);
	if (retval)
		goto out;

	acct_arg_size(bprm, 0);
	retval = exec_mmap(bprm->mm);
	if (retval)
		goto out;

	bprm->mm = NULL;

	/* Inlined unshare_sighand */
	{
		struct sighand_struct *oldsighand = me->sighand;
		if (refcount_read(&oldsighand->count) != 1) {
			struct sighand_struct *newsighand;

			newsighand =
				kmem_cache_alloc(sighand_cachep, GFP_KERNEL);
			if (!newsighand) {
				retval = -ENOMEM;
				goto out_unlock;
			}

			refcount_set(&newsighand->count, 1);
			memcpy(newsighand->action, oldsighand->action,
			       sizeof(newsighand->action));

			write_lock_irq(&tasklist_lock);
			spin_lock(&oldsighand->siglock);
			rcu_assign_pointer(me->sighand, newsighand);
			spin_unlock(&oldsighand->siglock);
			write_unlock_irq(&tasklist_lock);

			__cleanup_sighand(oldsighand);
		}
	}

	me->flags &= ~(PF_RANDOMIZE | PF_FORKNOEXEC | PF_NOFREEZE |
		       PF_NO_SETAFFINITY);
	flush_thread();
	me->personality &= ~bprm->per_clear;

	clear_syscall_work_syscall_user_dispatch(me);

	do_close_on_exec(me->files);

	if (bprm->secureexec) {
		me->pdeath_signal = 0;

		if (bprm->rlim_stack.rlim_cur > _STK_LIM)
			bprm->rlim_stack.rlim_cur = _STK_LIM;
	}

	/* me->sas_ss_sp = me->sas_ss_size = 0; removed - write-only fields */

	__set_task_comm(me, kbasename(bprm->filename), true);

	/* self_exec_id increment removed - write-only field */
	flush_signal_handlers(me, 0);

	retval = set_cred_ucounts(bprm->cred);
	if (retval < 0)
		goto out_unlock;

	/* security_bprm_committing_creds - empty stub */
	commit_creds(bprm->cred);
	bprm->cred = NULL;
	/* security_bprm_committed_creds, perf_event_exec/exit_task - stubs */
	/* bprm->have_execfd check removed - never set (kzalloc zeros) */
	return 0;

out_unlock:
	up_write(&me->signal->exec_update_lock);
out:
	return retval;
}

void setup_new_exec(struct linux_binprm *bprm)
{
	struct task_struct *me = current;

	arch_pick_mmap_layout(me->mm, &bprm->rlim_stack);

	up_write(&me->signal->exec_update_lock);
	mutex_unlock(&me->signal->cred_guard_mutex);
}

void finalize_exec(struct linux_binprm *bprm)
{
	task_lock(current->group_leader);
	current->signal->rlim[RLIMIT_STACK] = bprm->rlim_stack;
	task_unlock(current->group_leader);
}

/* prepare_bprm_creds inlined into bprm_execve */

static void free_bprm(struct linux_binprm *bprm)
{
	if (bprm->mm) {
		acct_arg_size(bprm, 0);
		mmput(bprm->mm);
	}
	if (bprm->cred) {
		mutex_unlock(&current->signal->cred_guard_mutex);
		abort_creds(bprm->cred);
	}
	if (bprm->file) {
		allow_write_access(bprm->file);
		fput(bprm->file);
	}
	/* bprm->executable check removed - never set (kzalloc zeros) */

	if (bprm->interp != bprm->filename)
		kfree(bprm->interp);
	kfree(bprm->fdpath);
	kfree(bprm);
}

static struct linux_binprm *alloc_bprm(int fd, struct filename *filename)
{
	struct linux_binprm *bprm = kzalloc(sizeof(*bprm), GFP_KERNEL);
	int retval = -ENOMEM;
	if (!bprm)
		goto out;

	if (fd == AT_FDCWD || filename->name[0] == '/') {
		bprm->filename = filename->name;
	} else {
		if (filename->name[0] == '\0')
			bprm->fdpath = kasprintf(GFP_KERNEL, "/dev/fd/%d", fd);
		else
			bprm->fdpath = kasprintf(GFP_KERNEL, "/dev/fd/%d/%s",
						 fd, filename->name);
		if (!bprm->fdpath)
			goto out_free;

		bprm->filename = bprm->fdpath;
	}
	bprm->interp = bprm->filename;

	retval = bprm_mm_init(bprm);
	if (retval)
		goto out_free;
	return bprm;

out_free:
	free_bprm(bprm);
out:
	return ERR_PTR(retval);
}

/* Removed: bprm_change_interp - never called */

/* check_unsafe_exec inlined into bprm_execve */

/* security_bprm_creds_from_file always returns 0 - simplified */
static void bprm_creds_from_file(struct linux_binprm *bprm)
{
	/* bprm->execfd_creds is never set (kzalloc zeros), so always use bprm->file */
	struct file *file = bprm->file;
	struct inode *inode;
	unsigned int mode;

	if (!mnt_may_suid(file->f_path.mnt) || task_no_new_privs(current))
		return;

	inode = file->f_path.dentry->d_inode;
	mode = READ_ONCE(inode->i_mode);
	if (!(mode & (S_ISUID | S_ISGID)))
		return;

	if (mode & S_ISUID) {
		bprm->per_clear |= PER_CLEAR_ON_SETID;
		bprm->cred->euid =
			i_uid_into_mnt(file_mnt_user_ns(file), inode);
	}

	if ((mode & (S_ISGID | S_IXGRP)) == (S_ISGID | S_IXGRP)) {
		bprm->per_clear |= PER_CLEAR_ON_SETID;
		bprm->cred->egid =
			i_gid_into_mnt(file_mnt_user_ns(file), inode);
	}
}

static int search_binary_handler(struct linux_binprm *bprm)
{
	struct linux_binfmt *fmt;
	int retval;
	loff_t pos = 0;

	/* Inlined prepare_binprm */
	memset(bprm->buf, 0, BINPRM_BUF_SIZE);
	retval = kernel_read(bprm->file, bprm->buf, BINPRM_BUF_SIZE, &pos);
	if (retval < 0)
		return retval;

	/* security_bprm_check always returns 0 - dead code removed */
	/* try_module_get always returns true - dead check removed */
	retval = -ENOENT;
	read_lock(&binfmt_lock);
	list_for_each_entry(fmt, &formats, lh) {
		read_unlock(&binfmt_lock);

		retval = fmt->load_binary(bprm);

		read_lock(&binfmt_lock);
		module_put(fmt->module);
		if (bprm->point_of_no_return || (retval != -ENOEXEC)) {
			read_unlock(&binfmt_lock);
			return retval;
		}
	}
	read_unlock(&binfmt_lock);
	return retval;
}

static int exec_binprm(struct linux_binprm *bprm)
{
	pid_t old_pid, old_vpid;
	int ret, depth;

	old_pid = current->pid;
	rcu_read_lock();
	old_vpid = task_pid_nr_ns(current, task_active_pid_ns(current->parent));
	rcu_read_unlock();

	for (depth = 0;; depth++) {
		struct file *exec;
		if (depth > 5)
			return -ELOOP;

		ret = search_binary_handler(bprm);
		if (ret < 0)
			return ret;
		if (!bprm->interpreter)
			break;

		exec = bprm->file;
		bprm->file = bprm->interpreter;
		bprm->interpreter = NULL;

		allow_write_access(exec);
		fput(exec);
	}

	/* audit_bprm - empty stub */
	ptrace_event(PTRACE_EVENT_EXEC, old_vpid);
	return 0;
}

static int bprm_execve(struct linux_binprm *bprm, int fd,
		       struct filename *filename, int flags)
{
	struct file *file;
	int retval;

	/* Inlined prepare_bprm_creds */
	if (mutex_lock_interruptible(&current->signal->cred_guard_mutex))
		return -ERESTARTNOINTR;
	bprm->cred = prepare_exec_creds();
	if (unlikely(!bprm->cred)) {
		mutex_unlock(&current->signal->cred_guard_mutex);
		return -ENOMEM;
	}

	/* inlined check_unsafe_exec */
	{
		struct task_struct *p = current, *t;
		unsigned n_fs;

		if (p->ptrace)
			bprm->unsafe |= LSM_UNSAFE_PTRACE;

		if (task_no_new_privs(current))
			bprm->unsafe |= LSM_UNSAFE_NO_NEW_PRIVS;

		t = p;
		n_fs = 1;
		spin_lock(&p->fs->lock);
		rcu_read_lock();
		while_each_thread(p, t)
		{
			if (t->fs == p->fs)
				n_fs++;
		}
		rcu_read_unlock();

		if (p->fs->users > n_fs)
			bprm->unsafe |= LSM_UNSAFE_SHARE;
		else
			p->fs->in_exec = 1;
		spin_unlock(&p->fs->lock);
	}

	file = do_open_execat(fd, filename, flags);
	retval = PTR_ERR(file);
	if (IS_ERR(file))
		goto out_unmark;

	/* sched_exec() - empty stub removed */
	bprm->file = file;

	if (bprm->fdpath && get_close_on_exec(fd))
		bprm->interp_flags |= BINPRM_FLAGS_PATH_INACCESSIBLE;

	/* security_bprm_creds_for_exec always returns 0 - dead code removed */
	retval = exec_binprm(bprm);
	if (retval < 0)
		goto out;

	current->fs->in_exec = 0;
	return retval;

out:

	if (bprm->point_of_no_return && !fatal_signal_pending(current))
		force_fatal_sig(SIGSEGV);

out_unmark:
	current->fs->in_exec = 0;

	return retval;
}

/* Removed: do_execveat_common - execve syscall is stubbed */

int kernel_execve(const char *kernel_filename, const char *const *argv,
		  const char *const *envp)
{
	struct filename *filename;
	struct linux_binprm *bprm;
	int fd = AT_FDCWD;
	int retval;

	if (WARN_ON_ONCE(current->flags & PF_KTHREAD))
		return -EINVAL;

	filename = getname_kernel(kernel_filename);
	if (IS_ERR(filename))
		return PTR_ERR(filename);

	bprm = alloc_bprm(fd, filename);
	if (IS_ERR(bprm)) {
		retval = PTR_ERR(bprm);
		goto out_ret;
	}

	retval = count_strings_kernel(argv);
	if (WARN_ON_ONCE(retval == 0))
		retval = -EINVAL;
	if (retval < 0)
		goto out_free;
	bprm->argc = retval;

	retval = count_strings_kernel(envp);
	if (retval < 0)
		goto out_free;
	bprm->envc = retval;

	/* Inlined bprm_stack_limits */
	{
		unsigned long limit = _STK_LIM / 4 * 3;
		unsigned long ptr_size;
		limit = min(limit, bprm->rlim_stack.rlim_cur / 4);
		limit = max_t(unsigned long, limit, ARG_MAX);
		ptr_size = (max(bprm->argc, 1) + bprm->envc) * sizeof(void *);
		if (limit <= ptr_size) {
			retval = -E2BIG;
			goto out_free;
		}
		bprm->argmin = bprm->p - (limit - ptr_size);
	}

	retval = copy_string_kernel(bprm->filename, bprm);
	if (retval < 0)
		goto out_free;
	bprm->exec = bprm->p;

	retval = copy_strings_kernel(bprm->envc, envp, bprm);
	if (retval < 0)
		goto out_free;

	retval = copy_strings_kernel(bprm->argc, argv, bprm);
	if (retval < 0)
		goto out_free;

	retval = bprm_execve(bprm, fd, filename, 0);
out_free:
	free_bprm(bprm);
out_ret:
	putname(filename);
	return retval;
}

/* Removed: do_execve - execve syscall is stubbed */

void set_binfmt(struct linux_binfmt *new)
{
	struct mm_struct *mm = current->mm;

	if (mm->binfmt)
		module_put(mm->binfmt->module);

	mm->binfmt = new;
	if (new)
		__module_get(new->module);
}

/* set_dumpable removed - was empty stub, call sites also removed */
/* execve/execveat replaced with COND_SYSCALL */
