
#include <linux/file.h>
#include <linux/sched/mm.h>
#include <linux/sched/signal.h>
#include <linux/highmem.h>
#include <linux/binfmts.h>
#include <linux/namei.h>

#include <asm/mmu_context.h>

#include "internal.h"

static LIST_HEAD(formats);
static DEFINE_RWLOCK(binfmt_lock);

void __register_binfmt(struct linux_binfmt *fmt, int insert)
{
	write_lock(&binfmt_lock);
	insert ? list_add(&fmt->lh, &formats) :
		 list_add_tail(&fmt->lh, &formats);
	write_unlock(&binfmt_lock);
}

static void acct_arg_size(struct linux_binprm *bprm, unsigned long pages)
{
	struct mm_struct *mm = current->mm;
	long diff = (long)(pages - bprm->vma_pages);

	if (!mm || !diff)
		return;

	bprm->vma_pages = pages;
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
	ret = get_user_pages_remote(bprm->mm, pos, 1, gup_flags, &page);
	mmap_read_unlock(bprm->mm);
	if (ret <= 0)
		return NULL;

	if (write)
		acct_arg_size(bprm, vma_pages(bprm->vma));

	return page;
}

static void put_arg_page(struct page *page)
{
	put_page(page);
}

static int __bprm_mm_init(struct linux_binprm *bprm)
{
	int err;
	struct vm_area_struct *vma = NULL;
	struct mm_struct *mm = bprm->mm;

	bprm->vma = vma = vm_area_alloc(mm);
	if (!vma)
		return -ENOMEM;
	vma_set_anonymous(vma);

	if (mmap_write_lock_killable(mm)) {
		err = -EINTR;
		goto err_free;
	}

	vma->vm_end = STACK_TOP_MAX;
	vma->vm_start = vma->vm_end - PAGE_SIZE;
	vma->vm_flags = VM_STACK_FLAGS;
	vma->vm_page_prot = vm_get_page_prot(vma->vm_flags);

	err = insert_vm_struct(mm, vma);
	if (err)
		goto err;

	mmap_write_unlock(mm);
	bprm->p = vma->vm_end - sizeof(void *);
	return 0;
err:
	mmap_write_unlock(mm);
err_free:
	bprm->vma = NULL;
	vm_area_free(vma);
	return err;
}

static bool valid_arg_len(struct linux_binprm *bprm, long len)
{
	return len <= MAX_ARG_STRLEN;
}

static int bprm_mm_init(struct linux_binprm *bprm)
{
	int err;
	struct mm_struct *mm = NULL;

	bprm->mm = mm = mm_alloc();
	err = -ENOMEM;
	if (!mm)
		goto err;

	task_lock(current->group_leader);
	bprm->rlim_stack = current->signal->rlim[RLIMIT_STACK];
	task_unlock(current->group_leader);

	err = __bprm_mm_init(bprm);
	if (err)
		goto err;

	return 0;

err:
	if (mm) {
		bprm->mm = NULL;
		mmdrop(mm);
	}

	return err;
}

static int count_strings_kernel(const char *const *argv)
{
	int i;

	if (!argv)
		return 0;

	for (i = 0; argv[i]; ++i) {
		if (i >= MAX_ARG_STRINGS)
			return -E2BIG;
		cond_resched();
	}
	return i;
}

static int bprm_stack_limits(struct linux_binprm *bprm)
{
	unsigned long limit, ptr_size;

	limit = _STK_LIM / 4 * 3;
	limit = min(limit, bprm->rlim_stack.rlim_cur / 4);

	limit = max_t(unsigned long, limit, ARG_MAX);

	ptr_size = (max(bprm->argc, 1) + bprm->envc) * sizeof(void *);
	if (limit <= ptr_size)
		return -E2BIG;
	limit -= ptr_size;

	bprm->argmin = bprm->p - limit;
	return 0;
}

int copy_string_kernel(const char *arg, struct linux_binprm *bprm)
{
	int len = strnlen(arg, MAX_ARG_STRLEN) + 1;
	unsigned long pos = bprm->p;

	if (len == 0)
		return -EFAULT;
	if (!valid_arg_len(bprm, len))
		return -E2BIG;

	arg += len;
	bprm->p -= len;
	if (IS_ENABLED(CONFIG_MMU) && bprm->p < bprm->argmin)
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
		put_arg_page(page);
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
		cond_resched();
	}
	return 0;
}

/*
 * The stack VMA is created at STACK_TOP_MAX and binfmt_elf passes
 * STACK_TOP (equal on x86-32; there is no stack randomization), so the
 * stack never needs to be shifted.
 */
int setup_arg_pages(struct linux_binprm *bprm, unsigned long stack_top)
{
	unsigned long ret;
	struct mm_struct *mm = current->mm;
	struct vm_area_struct *vma = bprm->vma;
	unsigned long vm_flags, stack_base, stack_size, stack_expand,
		rlim_stack;

	if (WARN_ON(vma->vm_end != PAGE_ALIGN(stack_top)))
		return -ENOMEM;

	mm->arg_start = bprm->p;

	if (mmap_write_lock_killable(mm))
		return -EINTR;

	/*
	 * __bprm_mm_init() created the stack VMA before binfmt_elf set the
	 * personality, so apply the final flags now (READ_IMPLIES_EXEC adds
	 * VM_EXEC). They can only gain access, and VM_EXEC has no PTE bit
	 * without NX (non-PAE x86-32), so the argument pages already mapped
	 * keep valid PTEs and need no change_protection().
	 */
	vm_flags = VM_STACK_FLAGS | mm->def_flags;
	WARN_ON_ONCE(vma->vm_flags & ~vm_flags & (VM_READ | VM_WRITE));
	vma->vm_flags = vm_flags;
	vma->vm_page_prot = vm_get_page_prot(vm_flags);

	stack_expand = 131072UL;
	stack_size = vma->vm_end - vma->vm_start;

	rlim_stack = bprm->rlim_stack.rlim_cur & PAGE_MASK;
	if (stack_size + stack_expand > rlim_stack)
		stack_base = vma->vm_end - rlim_stack;
	else
		stack_base = vma->vm_start - stack_expand;
	ret = expand_stack(vma, stack_base);
	if (ret)
		ret = -EFAULT;

	mmap_write_unlock(mm);
	return ret;
}

static struct file *do_open_execat(struct filename *name)
{
	struct file *file;
	int err;
	struct open_flags open_exec_flags = {
		.open_flag = O_LARGEFILE | O_RDONLY | __FMODE_EXEC,
		.acc_mode = MAY_EXEC,
		.intent = LOOKUP_OPEN,
		.lookup_flags = LOOKUP_FOLLOW,
	};

	file = do_filp_open(name, &open_exec_flags);
	if (IS_ERR(file))
		goto out;

	err = -EACCES;
	if (WARN_ON_ONCE(!S_ISREG(file_inode(file)->i_mode)))
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

static int exec_mmap(struct mm_struct *mm)
{
	struct task_struct *tsk;
	struct mm_struct *old_mm, *active_mm;
	int ret;

	tsk = current;
	old_mm = current->mm;
	exec_mm_release(tsk, old_mm);
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

	local_irq_disable();
	active_mm = tsk->active_mm;
	tsk->active_mm = mm;
	tsk->mm = mm;

	local_irq_enable();
	activate_mm(active_mm, mm);
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

	/*
	 * No set-id execution: the initramfs is root-owned and no file is
	 * ever set-uid/gid, so the bprm credentials are the caller's.
	 * The only exec caller (init) is a single-threaded group leader.
	 */
	bprm->point_of_no_return = true;
	me->exit_signal = SIGCHLD;

	retval = set_mm_exe_file(bprm->mm, bprm->file);
	if (retval)
		goto out;

	acct_arg_size(bprm, 0);
	retval = exec_mmap(bprm->mm);
	if (retval)
		goto out;

	bprm->mm = NULL;

	/*
	 * unshare_sighand() removed: the only execve() callers (init via
	 * user_mode_thread, kernel_execve) never pass CLONE_SIGHAND, so
	 * sighand->count is always 1 and the unshare branch was dead.
	 */

	__set_task_comm(me, kbasename(bprm->filename), true);

	commit_creds(bprm->cred);
	bprm->cred = NULL;

	return 0;

out:
	return retval;
}

void setup_new_exec(struct linux_binprm *bprm)
{
	struct task_struct *me = current;

	up_write(&me->signal->exec_update_lock);
	mutex_unlock(&me->signal->cred_guard_mutex);
}

void finalize_exec(struct linux_binprm *bprm)
{
	task_lock(current->group_leader);
	current->signal->rlim[RLIMIT_STACK] = bprm->rlim_stack;
	task_unlock(current->group_leader);
}

static int prepare_bprm_creds(struct linux_binprm *bprm)
{
	if (mutex_lock_interruptible(&current->signal->cred_guard_mutex))
		return -ERESTARTNOINTR;

	bprm->cred = prepare_exec_creds();
	if (likely(bprm->cred))
		return 0;

	mutex_unlock(&current->signal->cred_guard_mutex);
	return -ENOMEM;
}

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

	kfree(bprm);
}

static struct linux_binprm *alloc_bprm(struct filename *filename)
{
	struct linux_binprm *bprm = kzalloc(sizeof(*bprm), GFP_KERNEL);
	int retval = -ENOMEM;
	if (!bprm)
		goto out;

	/* AT_FDCWD-only exec: filename used directly, /dev/fd/N branch dead. */
	bprm->filename = filename->name;

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

static int prepare_binprm(struct linux_binprm *bprm)
{
	loff_t pos = 0;

	memset(bprm->buf, 0, BINPRM_BUF_SIZE);
	return kernel_read(bprm->file, bprm->buf, BINPRM_BUF_SIZE, &pos);
}

static int search_binary_handler(struct linux_binprm *bprm)
{
	struct linux_binfmt *fmt;
	int retval;

	retval = prepare_binprm(bprm);
	if (retval < 0)
		return retval;

	retval = -ENOENT;
	read_lock(&binfmt_lock);
	list_for_each_entry(fmt, &formats, lh) {
		read_unlock(&binfmt_lock);

		retval = fmt->load_binary(bprm);

		read_lock(&binfmt_lock);
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
	int ret = search_binary_handler(bprm);

	return ret < 0 ? ret : 0;
}

static int bprm_execve(struct linux_binprm *bprm, struct filename *filename)
{
	struct file *file;
	int retval;

	retval = prepare_bprm_creds(bprm);
	if (retval)
		return retval;

	file = do_open_execat(filename);
	retval = PTR_ERR(file);
	if (IS_ERR(file))
		return retval;

	bprm->file = file;
	return exec_binprm(bprm);
}

int kernel_execve(const char *kernel_filename, const char *const *argv,
		  const char *const *envp)
{
	struct filename *filename;
	struct linux_binprm *bprm;
	int retval;

	if (WARN_ON_ONCE(current->flags & PF_KTHREAD))
		return -EINVAL;

	filename = getname_kernel(kernel_filename);
	if (IS_ERR(filename))
		return PTR_ERR(filename);

	bprm = alloc_bprm(filename);
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

	retval = bprm_stack_limits(bprm);
	if (retval < 0)
		goto out_free;

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

	retval = bprm_execve(bprm, filename);
out_free:
	free_bprm(bprm);
out_ret:
	putname(filename);
	return retval;
}
