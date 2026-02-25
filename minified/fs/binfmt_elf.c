
#include <linux/module.h>
#include <linux/mman.h>
#include <linux/binfmts.h>
#include <linux/ptrace.h>
#include <linux/pagemap.h>
#include <linux/sched/task_stack.h>

static int load_elf_binary(struct linux_binprm *bprm);

/* ELF_EXEC_PAGESIZE = PAGE_SIZE = 4096 on x86 */
#define ELF_MIN_ALIGN PAGE_SIZE

#define ELF_PAGESTART(_v) ((_v) & ~(int)(ELF_MIN_ALIGN - 1))
#define ELF_PAGEOFFSET(_v) ((_v) & (ELF_MIN_ALIGN - 1))
#define ELF_PAGEALIGN(_v) (((_v) + ELF_MIN_ALIGN - 1) & ~(ELF_MIN_ALIGN - 1))

static struct linux_binfmt elf_format = {
	.module = THIS_MODULE,
	.load_binary = load_elf_binary,
};

#define BAD_ADDR(x) (unlikely((unsigned long)(x) >= TASK_SIZE))

static int set_brk(unsigned long start, unsigned long end, int prot)
{
	start = ELF_PAGEALIGN(start);
	end = ELF_PAGEALIGN(end);
	if (end > start) {
		int error = vm_brk_flags(start, end - start,
					 prot & PROT_EXEC ? VM_EXEC : 0);
		if (error)
			return error;
	}
	return 0;
}

#define STACK_ADD(sp, items) ((elf_addr_t __user *)(sp) - (items))
#define STACK_ROUND(sp, items) (((unsigned long)(sp - items)) & ~15UL)

static int create_elf_tables(struct linux_binprm *bprm,
			     const struct elfhdr *exec,
			     unsigned long interp_load_addr,
			     unsigned long e_entry, unsigned long phdr_addr)
{
	unsigned long p = bprm->p;
	int argc = bprm->argc;
	int envc = bprm->envc;
	elf_addr_t __user *sp;
	int items;
	elf_addr_t *elf_info;
	int ei_index;
	struct mm_struct *mm = current->mm;
	struct vm_area_struct *vma;

	p = arch_align_stack(p);

	elf_info = (elf_addr_t *)mm->saved_auxv;

#define NEW_AUX_ENT(id, val)       \
	do {                       \
		*elf_info++ = id;  \
		*elf_info++ = val; \
	} while (0)

	/* Minimal aux vector - /init is bare asm, doesn't read these */
	NEW_AUX_ENT(AT_PAGESZ, ELF_EXEC_PAGESIZE);
	NEW_AUX_ENT(AT_PHDR, phdr_addr);
	NEW_AUX_ENT(AT_PHENT, sizeof(struct elf_phdr));
	NEW_AUX_ENT(AT_PHNUM, exec->e_phnum);
	NEW_AUX_ENT(AT_ENTRY, e_entry);
#undef NEW_AUX_ENT

	memset(elf_info, 0,
	       (char *)mm->saved_auxv + sizeof(mm->saved_auxv) -
		       (char *)elf_info);

	elf_info += 2;

	ei_index = elf_info - (elf_addr_t *)mm->saved_auxv;
	sp = STACK_ADD(p, ei_index);

	items = (argc + 1) + (envc + 1) + 1;
	bprm->p = STACK_ROUND(sp, items);

	sp = (elf_addr_t __user *)bprm->p;

	if (mmap_read_lock_killable(mm))
		return -EINTR;
	vma = find_extend_vma(mm, bprm->p);
	mmap_read_unlock(mm);
	if (!vma)
		return -EFAULT;

	if (put_user(argc, sp++))
		return -EFAULT;

	p = bprm->p;
	while (argc-- > 0) {
		size_t len;
		if (put_user((elf_addr_t)p, sp++))
			return -EFAULT;
		len = strnlen_user((void __user *)p, MAX_ARG_STRLEN);
		if (!len || len > MAX_ARG_STRLEN)
			return -EINVAL;
		p += len;
	}
	if (put_user(0, sp++))
		return -EFAULT;

	while (envc-- > 0) {
		size_t len;
		if (put_user((elf_addr_t)p, sp++))
			return -EFAULT;
		len = strnlen_user((void __user *)p, MAX_ARG_STRLEN);
		if (!len || len > MAX_ARG_STRLEN)
			return -EINVAL;
		p += len;
	}
	if (put_user(0, sp++))
		return -EFAULT;

	if (copy_to_user(sp, mm->saved_auxv, ei_index * sizeof(elf_addr_t)))
		return -EFAULT;
	return 0;
}

static unsigned long elf_map(struct file *filep, unsigned long addr,
			     const struct elf_phdr *eppnt, int prot, int type,
			     unsigned long total_size)
{
	unsigned long map_addr;
	unsigned long size = eppnt->p_filesz + ELF_PAGEOFFSET(eppnt->p_vaddr);
	unsigned long off = eppnt->p_offset - ELF_PAGEOFFSET(eppnt->p_vaddr);
	addr = ELF_PAGESTART(addr);
	size = ELF_PAGEALIGN(size);

	if (!size)
		return addr;

	map_addr = vm_mmap(filep, addr, size, prot, type, off);

	if ((type & MAP_FIXED_NOREPLACE) &&
	    PTR_ERR((void *)map_addr) == -EEXIST)
		pr_info("%d (%s): Uhuuh, elf segment at %px requested but the memory is mapped already\n",
			current->pid, current->comm, (void *)addr);

	return (map_addr);
}

static int elf_read(struct file *file, void *buf, size_t len, loff_t pos)
{
	ssize_t rv;

	rv = kernel_read(file, buf, len, &pos);
	if (unlikely(rv != len)) {
		return (rv < 0) ? rv : -EIO;
	}
	return 0;
}

static struct elf_phdr *load_elf_phdrs(const struct elfhdr *elf_ex,
				       struct file *elf_file)
{
	struct elf_phdr *elf_phdata = NULL;
	int retval, err = -1;
	unsigned int size;

	if (elf_ex->e_phentsize != sizeof(struct elf_phdr))
		goto out;

	size = sizeof(struct elf_phdr) * elf_ex->e_phnum;
	if (size == 0 || size > 65536 || size > ELF_MIN_ALIGN)
		goto out;

	elf_phdata = kmalloc(size, GFP_KERNEL);
	if (!elf_phdata)
		goto out;

	retval = elf_read(elf_file, elf_phdata, size, elf_ex->e_phoff);
	if (retval < 0) {
		err = retval;
		goto out;
	}

	err = 0;
out:
	if (err) {
		kfree(elf_phdata);
		elf_phdata = NULL;
	}
	return elf_phdata;
}

static inline int make_prot(u32 p_flags)
{
	int prot = 0;

	if (p_flags & PF_R)
		prot |= PROT_READ;
	if (p_flags & PF_W)
		prot |= PROT_WRITE;
	if (p_flags & PF_X)
		prot |= PROT_EXEC;

	return prot;
}

static int load_elf_binary(struct linux_binprm *bprm)
{
	unsigned long load_bias = 0, phdr_addr = 0;
	int first_pt_load = 1;
	unsigned long error;
	struct elf_phdr *elf_ppnt, *elf_phdata;
	unsigned long elf_bss, elf_brk;
	int bss_prot = 0;
	int retval, i;
	unsigned long elf_entry;
	unsigned long e_entry;
	int executable_stack = EXSTACK_DEFAULT;
	struct elfhdr *elf_ex = (struct elfhdr *)bprm->buf;
	struct pt_regs *regs;

	retval = -ENOEXEC;

	if (memcmp(elf_ex->e_ident, ELFMAG, SELFMAG) != 0)
		goto out;

	if (elf_ex->e_type != ET_EXEC)
		goto out;
	if (!elf_check_arch(elf_ex))
		goto out;
	if (!bprm->file->f_op->mmap)
		goto out;

	elf_phdata = load_elf_phdrs(elf_ex, bprm->file);
	if (!elf_phdata)
		goto out;

	elf_ppnt = elf_phdata;
	for (i = 0; i < elf_ex->e_phnum; i++, elf_ppnt++)
		switch (elf_ppnt->p_type) {
		case PT_GNU_STACK:
			if (elf_ppnt->p_flags & PF_X)
				executable_stack = EXSTACK_ENABLE_X;
			else
				executable_stack = EXSTACK_DISABLE_X;
			break;
		}

	retval = begin_new_exec(bprm);
	if (retval)
		goto out_free_ph;

	SET_PERSONALITY2(*elf_ex, NULL);
	if (elf_read_implies_exec(*elf_ex, executable_stack))
		current->personality |= READ_IMPLIES_EXEC;

	setup_new_exec(bprm);

	retval = setup_arg_pages(bprm, PAGE_ALIGN(STACK_TOP), executable_stack);
	if (retval < 0)
		goto out_free_ph;

	elf_bss = 0;
	elf_brk = 0;

	for (i = 0, elf_ppnt = elf_phdata; i < elf_ex->e_phnum;
	     i++, elf_ppnt++) {
		int elf_prot, elf_flags;
		unsigned long k, vaddr;

		if (elf_ppnt->p_type != PT_LOAD)
			continue;

		if (unlikely(elf_brk > elf_bss)) {
			unsigned long nbyte;

			retval = set_brk(elf_bss + load_bias,
					 elf_brk + load_bias, bss_prot);
			if (retval)
				goto out_free_ph;
			nbyte = ELF_PAGEOFFSET(elf_bss);
			if (nbyte) {
				nbyte = ELF_MIN_ALIGN - nbyte;
				if (nbyte > elf_brk - elf_bss)
					nbyte = elf_brk - elf_bss;
				if (clear_user((void __user *)elf_bss +
						       load_bias,
					       nbyte)) {
				}
			}
		}

		elf_prot = make_prot(elf_ppnt->p_flags);

		elf_flags = MAP_PRIVATE;

		vaddr = elf_ppnt->p_vaddr;

		if (!first_pt_load)
			elf_flags |= MAP_FIXED;
		else
			elf_flags |= MAP_FIXED_NOREPLACE;

		error = elf_map(bprm->file, load_bias + vaddr, elf_ppnt,
				elf_prot, elf_flags, 0);
		if (BAD_ADDR(error)) {
			retval = IS_ERR((void *)error) ?
					 PTR_ERR((void *)error) :
					 -EINVAL;
			goto out_free_ph;
		}

		if (first_pt_load)
			first_pt_load = 0;

		if (elf_ppnt->p_offset <= elf_ex->e_phoff &&
		    elf_ex->e_phoff < elf_ppnt->p_offset + elf_ppnt->p_filesz) {
			phdr_addr = elf_ex->e_phoff - elf_ppnt->p_offset +
				    elf_ppnt->p_vaddr;
		}

		k = elf_ppnt->p_vaddr;

		if (BAD_ADDR(k) || elf_ppnt->p_filesz > elf_ppnt->p_memsz ||
		    elf_ppnt->p_memsz > TASK_SIZE ||
		    TASK_SIZE - elf_ppnt->p_memsz < k) {
			retval = -EINVAL;
			goto out_free_ph;
		}

		k = elf_ppnt->p_vaddr + elf_ppnt->p_filesz;

		if (k > elf_bss)
			elf_bss = k;
		k = elf_ppnt->p_vaddr + elf_ppnt->p_memsz;
		if (k > elf_brk) {
			bss_prot = elf_prot;
			elf_brk = k;
		}
	}

	e_entry = elf_ex->e_entry + load_bias;
	phdr_addr += load_bias;
	elf_bss += load_bias;
	elf_brk += load_bias;

	retval = set_brk(elf_bss, elf_brk, bss_prot);
	if (retval)
		goto out_free_ph;
	if (likely(elf_bss != elf_brk)) {
		unsigned long nbyte = ELF_PAGEOFFSET(elf_bss);
		if (nbyte) {
			nbyte = ELF_MIN_ALIGN - nbyte;
			if (clear_user((void __user *)elf_bss, nbyte)) {
				retval = -EFAULT;
				goto out_free_ph;
			}
		}
	}

	elf_entry = e_entry;
	if (BAD_ADDR(elf_entry)) {
		retval = -EINVAL;
		goto out_free_ph;
	}

	kfree(elf_phdata);

	set_binfmt(&elf_format);

	retval = create_elf_tables(bprm, elf_ex, 0, e_entry, phdr_addr);
	if (retval < 0)
		goto out;

	regs = current_pt_regs();
	ELF_PLAT_INIT(regs, 0);

	task_lock(current->group_leader);
	current->signal->rlim[RLIMIT_STACK] = bprm->rlim_stack;
	task_unlock(current->group_leader);
	START_THREAD(elf_ex, regs, elf_entry, bprm->p);
	retval = 0;
out:
	return retval;

out_free_ph:
	kfree(elf_phdata);
	goto out;
}

static int __init init_elf_binfmt(void)
{
	register_binfmt(&elf_format);
	return 0;
}

core_initcall(init_elf_binfmt);
MODULE_LICENSE("GPL");
