
#include <linux/file.h>
#include <linux/sched/mm.h>
#include <linux/binfmts.h>
#include <linux/namei.h>
extern unsigned long mmap_min_addr;

#include <asm/mmu_context.h>
#include <asm/tlb.h>

#include "internal.h"

/* Single binfmt (ELF) - list/lock replaced with direct pointer */
static struct linux_binfmt *the_binfmt;

void __register_binfmt(struct linux_binfmt *fmt)
{
	the_binfmt = fmt;
}

bool path_noexec(const struct path *path)
{
	return (path->mnt->mnt_flags & MNT_NOEXEC) ||
	       (path->mnt->mnt_sb->s_iflags & SB_I_NOEXEC);
}

static int count_strings_kernel(const char *const *p)
{
	int i = 0;
	if (p)
		for (; p[i]; i++)
			;
	return i;
}

static int copy_string_kernel(const char *arg, struct linux_binprm *bprm)
{
	int len = strnlen(arg, MAX_ARG_STRLEN) + 1;
	unsigned long pos = bprm->p;

	if (len == 0)
		return -EFAULT;
	if (len > MAX_ARG_STRLEN)
		return -E2BIG;

	arg += len;
	bprm->p -= len;
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

		{
			int ret;
			mmap_read_lock(bprm->mm);
			ret = get_user_pages_remote(bprm->mm, pos, 1,
						    FOLL_FORCE | FOLL_WRITE,
						    &page, NULL, NULL);
			mmap_read_unlock(bprm->mm);
			if (ret <= 0)
				return -E2BIG;
		}
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
	}
	return 0;
}

int setup_arg_pages(struct linux_binprm *bprm, unsigned long stack_top,
		    int executable_stack)
{
	unsigned long ret;
	unsigned long stack_shift;
	struct mm_struct *mm = current->mm;
	struct vm_area_struct *vma = bprm->vma;
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

	bprm->exec -= stack_shift;

	if (mmap_write_lock_killable(mm))
		return -EINTR;

	if (stack_shift) {
		unsigned long old_start = vma->vm_start;
		unsigned long old_end = vma->vm_end;
		unsigned long length = old_end - old_start;
		unsigned long new_start = old_start - stack_shift;
		unsigned long new_end = old_end - stack_shift;
		struct mmu_gather tlb;

		if (vma != find_vma(mm, new_start)) {
			ret = -EFAULT;
			goto out_unlock;
		}
		if (vma_adjust(vma, new_start, old_end, vma->vm_pgoff, NULL)) {
			ret = -ENOMEM;
			goto out_unlock;
		}
		if (length != move_page_tables(vma, old_start, vma, new_start,
					       length, false)) {
			ret = -ENOMEM;
			goto out_unlock;
		}
		lru_add_drain();
		tlb_gather_mmu(&tlb, mm, false);
		if (new_end > old_start)
			free_pgd_range(&tlb, new_end, old_end, new_end,
				       vma->vm_next ? vma->vm_next->vm_start :
						      USER_PGTABLES_CEILING);
		else
			free_pgd_range(&tlb, old_start, old_end, new_end,
				       vma->vm_next ? vma->vm_next->vm_start :
						      USER_PGTABLES_CEILING);
		tlb_finish_mmu(&tlb);
		vma_adjust(vma, new_start, new_end, vma->vm_pgoff, NULL);
	}

	vma->vm_flags &= ~VM_STACK_INCOMPLETE_SETUP;

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

out_unlock:
	mmap_write_unlock(mm);
	return ret;
}

void __set_task_comm(struct task_struct *tsk, const char *buf, bool exec)
{
	task_lock(tsk);
	{
		size_t len = strlcpy(tsk->comm, buf, sizeof(tsk->comm));
		if (len < sizeof(tsk->comm))
			memset(tsk->comm + len, 0, sizeof(tsk->comm) - len);
	}
	task_unlock(tsk);
}

int begin_new_exec(struct linux_binprm *bprm)
{
	struct task_struct *me = current;
	int retval;

	bprm->point_of_no_return = true;

	retval = set_mm_exe_file(bprm->mm, bprm->file);
	if (retval)
		goto out;

	{
		struct mm_struct *mm = bprm->mm;
		struct task_struct *tsk = current;
		struct mm_struct *active_mm;

		deactivate_mm(tsk, NULL);
		down_write(&tsk->signal->exec_update_lock);
		task_lock(tsk);
		local_irq_disable();
		active_mm = tsk->active_mm;
		tsk->active_mm = mm;
		tsk->mm = mm;
		local_irq_enable();
		activate_mm(active_mm, mm);
		task_unlock(tsk);
		mmdrop(active_mm);
	}

	bprm->mm = NULL;

	me->flags &= ~(PF_FORKNOEXEC | PF_NOFREEZE | PF_NO_SETAFFINITY);
	flush_thread();

	{
		const char *tail = strrchr(bprm->filename, '/');
		__set_task_comm(me, tail ? tail + 1 : bprm->filename, true);
	}

	commit_creds(bprm->cred);
	bprm->cred = NULL;
	/* security_bprm_committed_creds, perf_event_exec/exit_task - stubs */
	return 0;

out:
	return retval;
}

void setup_new_exec(struct linux_binprm *bprm)
{
	struct task_struct *me = current;

	arch_pick_mmap_layout(me->mm, &bprm->rlim_stack);

	up_write(&me->signal->exec_update_lock);
}

static struct linux_binprm *alloc_bprm(int fd, struct filename *filename)
{
	struct linux_binprm *bprm = kzalloc(sizeof(*bprm), GFP_KERNEL);
	if (!bprm)
		panic("alloc_bprm: kzalloc");
	bprm->filename = filename->name;
	bprm->mm = mm_alloc();
	if (!bprm->mm)
		panic("alloc_bprm: mm_alloc");
	task_lock(current->group_leader);
	bprm->rlim_stack = current->signal->rlim[RLIMIT_STACK];
	task_unlock(current->group_leader);
	bprm->vma = vm_area_alloc(bprm->mm);
	if (!bprm->vma)
		panic("alloc_bprm: vm_area_alloc");
	vma_set_anonymous(bprm->vma);
	down_write(&bprm->mm->mmap_lock);
	bprm->vma->vm_end = STACK_TOP_MAX;
	bprm->vma->vm_start = bprm->vma->vm_end - PAGE_SIZE;
	bprm->vma->vm_flags = VM_STACK_FLAGS | VM_STACK_INCOMPLETE_SETUP;
	bprm->vma->vm_page_prot = vm_get_page_prot(bprm->vma->vm_flags);
	if (insert_vm_struct(bprm->mm, bprm->vma))
		panic("alloc_bprm: insert_vm_struct");
	mmap_write_unlock(bprm->mm);
	bprm->p = bprm->vma->vm_end - sizeof(void *);
	return bprm;
}

int kernel_execve(const char *kernel_filename, const char *const *argv,
		  const char *const *envp)
{
	struct filename *filename;
	struct linux_binprm *bprm;
	struct open_flags open_exec_flags = {
		.open_flag = O_LARGEFILE | O_RDONLY | __FMODE_EXEC,
		.acc_mode = MAY_EXEC,
		.intent = LOOKUP_OPEN,
		.lookup_flags = LOOKUP_FOLLOW,
	};
	loff_t pos = 0;
	int retval;

	if (current->flags & PF_KTHREAD)
		return -EINVAL;

	filename = getname_kernel(kernel_filename);
	if (IS_ERR(filename))
		return PTR_ERR(filename);

	bprm = alloc_bprm(AT_FDCWD, filename);
	bprm->argc = count_strings_kernel(argv);
	bprm->envc = count_strings_kernel(envp);
	bprm->argmin = bprm->p - PAGE_SIZE;

	retval = copy_string_kernel(bprm->filename, bprm);
	if (retval < 0)
		goto out;
	bprm->exec = bprm->p;

	retval = copy_strings_kernel(bprm->envc, envp, bprm);
	if (retval < 0)
		goto out;

	retval = copy_strings_kernel(bprm->argc, argv, bprm);
	if (retval < 0)
		goto out;

	bprm->cred = prepare_creds();
	if (!bprm->cred) {
		retval = -ENOMEM;
		goto out;
	}

	bprm->file = do_filp_open(AT_FDCWD, filename, &open_exec_flags);
	if (IS_ERR(bprm->file)) {
		retval = PTR_ERR(bprm->file);
		goto out;
	}

	memset(bprm->buf, 0, BINPRM_BUF_SIZE);
	retval = kernel_read(bprm->file, bprm->buf, BINPRM_BUF_SIZE, &pos);
	if (retval >= 0)
		retval = the_binfmt->load_binary(bprm);

out:
	if (bprm->mm)
		mmput(bprm->mm);
	if (bprm->cred)
		put_cred(bprm->cred);
	if (bprm->file)
		fput(bprm->file);
	putname(filename);
	return retval;
}

void set_binfmt(struct linux_binfmt *new)
{
	current->mm->binfmt = new;
}

/* execve/execveat replaced with COND_SYSCALL */
