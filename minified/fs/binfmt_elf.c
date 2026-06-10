
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/log2.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/errno.h>
#include <linux/signal.h>
#include <linux/binfmts.h>
#include <linux/string.h>
#include <linux/file.h>
#include <linux/slab.h>
#include <linux/personality.h>
#include <linux/ptrace.h>
#include <linux/init.h>
#include <linux/highuid.h>
#include <linux/compiler.h>
#include <linux/highmem.h>
#include <linux/hugetlb.h>
#include <linux/pagemap.h>
#include <linux/vmalloc.h>
#include <linux/random.h>
#include <linux/elf.h>
#include <linux/utsname.h>
#include <linux/sched.h>
#include <linux/sched/coredump.h>
#include <linux/sched/task_stack.h>
#include <linux/sched/cputime.h>
#include <linux/sizes.h>
#include <linux/types.h>
#include <linux/cred.h>
#include <linux/uaccess.h>
#include <asm/param.h>
#include <asm/page.h>

#ifndef ELF_COMPAT
#define ELF_COMPAT 0
#endif

#ifndef user_long_t
#define user_long_t long
#endif
#ifndef user_siginfo_t
#define user_siginfo_t siginfo_t
#endif

#ifndef elf_check_fdpic
#define elf_check_fdpic(ex) false
#endif

static int load_elf_binary(struct linux_binprm *bprm);

#define load_elf_library NULL

#if ELF_EXEC_PAGESIZE > PAGE_SIZE
#define ELF_MIN_ALIGN	ELF_EXEC_PAGESIZE
#else
#define ELF_MIN_ALIGN	PAGE_SIZE
#endif

#ifndef ELF_CORE_EFLAGS
#define ELF_CORE_EFLAGS	0
#endif

#define ELF_PAGESTART(_v) ((_v) & ~(int)(ELF_MIN_ALIGN-1))
#define ELF_PAGEOFFSET(_v) ((_v) & (ELF_MIN_ALIGN-1))
#define ELF_PAGEALIGN(_v) (((_v) + ELF_MIN_ALIGN - 1) & ~(ELF_MIN_ALIGN - 1))

static struct linux_binfmt elf_format = {
	.module		= THIS_MODULE,
	.load_binary	= load_elf_binary,
	.load_shlib	= load_elf_library,
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
	current->mm->start_brk = current->mm->brk = end;
	return 0;
}

static int padzero(unsigned long elf_bss)
{
	unsigned long nbyte;

	nbyte = ELF_PAGEOFFSET(elf_bss);
	if (nbyte) {
		nbyte = ELF_MIN_ALIGN - nbyte;
		if (clear_user((void __user *) elf_bss, nbyte))
			return -EFAULT;
	}
	return 0;
}

#define STACK_ADD(sp, items) ((elf_addr_t __user *)(sp) - (items))
#define STACK_ROUND(sp, items) \
	(((unsigned long) (sp - items)) &~ 15UL)
#define STACK_ALLOC(sp, len) (sp -= len)

#ifndef ELF_BASE_PLATFORM
#define ELF_BASE_PLATFORM NULL
#endif

static int
create_elf_tables(struct linux_binprm *bprm, const struct elfhdr *exec,
		unsigned long interp_load_addr,
		unsigned long e_entry, unsigned long phdr_addr)
{
	struct mm_struct *mm = current->mm;
	unsigned long p = bprm->p;
	int argc = bprm->argc;
	int envc = bprm->envc;
	elf_addr_t __user *sp;
	elf_addr_t __user *u_platform;
	elf_addr_t __user *u_base_platform;
	elf_addr_t __user *u_rand_bytes;
	const char *k_platform = ELF_PLATFORM;
	const char *k_base_platform = ELF_BASE_PLATFORM;
	unsigned char k_rand_bytes[16];
	int items;
	elf_addr_t *elf_info;
	elf_addr_t flags = 0;
	int ei_index;
	const struct cred *cred = current_cred();
	struct vm_area_struct *vma;

	 

	p = arch_align_stack(p);

	 
	u_platform = NULL;
	if (k_platform) {
		size_t len = strlen(k_platform) + 1;

		u_platform = (elf_addr_t __user *)STACK_ALLOC(p, len);
		if (copy_to_user(u_platform, k_platform, len))
			return -EFAULT;
	}

	 
	u_base_platform = NULL;
	if (k_base_platform) {
		size_t len = strlen(k_base_platform) + 1;

		u_base_platform = (elf_addr_t __user *)STACK_ALLOC(p, len);
		if (copy_to_user(u_base_platform, k_base_platform, len))
			return -EFAULT;
	}

	 
	get_random_bytes(k_rand_bytes, sizeof(k_rand_bytes));
	u_rand_bytes = (elf_addr_t __user *)
		       STACK_ALLOC(p, sizeof(k_rand_bytes));
	if (copy_to_user(u_rand_bytes, k_rand_bytes, sizeof(k_rand_bytes)))
		return -EFAULT;

	 
	elf_info = (elf_addr_t *)mm->saved_auxv;
	 
#define NEW_AUX_ENT(id, val) \
	do { \
		*elf_info++ = id; \
		*elf_info++ = val; \
	} while (0)

#ifdef ARCH_DLINFO
	 
	ARCH_DLINFO;
#endif
	NEW_AUX_ENT(AT_HWCAP, ELF_HWCAP);
	NEW_AUX_ENT(AT_PAGESZ, ELF_EXEC_PAGESIZE);
	NEW_AUX_ENT(AT_CLKTCK, CLOCKS_PER_SEC);
	NEW_AUX_ENT(AT_PHDR, phdr_addr);
	NEW_AUX_ENT(AT_PHENT, sizeof(struct elf_phdr));
	NEW_AUX_ENT(AT_PHNUM, exec->e_phnum);
	NEW_AUX_ENT(AT_BASE, interp_load_addr);
	if (bprm->interp_flags & BINPRM_FLAGS_PRESERVE_ARGV0)
		flags |= AT_FLAGS_PRESERVE_ARGV0;
	NEW_AUX_ENT(AT_FLAGS, flags);
	NEW_AUX_ENT(AT_ENTRY, e_entry);
	NEW_AUX_ENT(AT_UID, from_kuid_munged(cred->user_ns, cred->uid));
	NEW_AUX_ENT(AT_EUID, from_kuid_munged(cred->user_ns, cred->euid));
	NEW_AUX_ENT(AT_GID, from_kgid_munged(cred->user_ns, cred->gid));
	NEW_AUX_ENT(AT_EGID, from_kgid_munged(cred->user_ns, cred->egid));
	NEW_AUX_ENT(AT_SECURE, bprm->secureexec);
	NEW_AUX_ENT(AT_RANDOM, (elf_addr_t)(unsigned long)u_rand_bytes);
#ifdef ELF_HWCAP2
	NEW_AUX_ENT(AT_HWCAP2, ELF_HWCAP2);
#endif
	NEW_AUX_ENT(AT_EXECFN, bprm->exec);
	if (k_platform) {
		NEW_AUX_ENT(AT_PLATFORM,
			    (elf_addr_t)(unsigned long)u_platform);
	}
	if (k_base_platform) {
		NEW_AUX_ENT(AT_BASE_PLATFORM,
			    (elf_addr_t)(unsigned long)u_base_platform);
	}
#undef NEW_AUX_ENT
	 
	memset(elf_info, 0, (char *)mm->saved_auxv +
			sizeof(mm->saved_auxv) - (char *)elf_info);

	 
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

	 
	p = mm->arg_end = mm->arg_start;
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
	mm->arg_end = p;

	 
	mm->env_end = mm->env_start = p;
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
	mm->env_end = p;

	 
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

	 
	if (total_size) {
		total_size = ELF_PAGEALIGN(total_size);
		map_addr = vm_mmap(filep, addr, total_size, prot, type, off);
		if (!BAD_ADDR(map_addr))
			vm_munmap(map_addr+size, total_size-size);
	} else
		map_addr = vm_mmap(filep, addr, size, prot, type, off);

	if ((type & MAP_FIXED_NOREPLACE) &&
	    PTR_ERR((void *)map_addr) == -EEXIST)
		pr_info("%d (%s): Uhuuh, elf segment at %px requested but the memory is mapped already\n",
			task_pid_nr(current), current->comm, (void *)addr);

	return(map_addr);
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

#ifndef CONFIG_ARCH_BINFMT_ELF_STATE

struct arch_elf_state {
};

#define INIT_ARCH_ELF_STATE {}

static inline int arch_elf_pt_proc(struct elfhdr *ehdr,
				   struct elf_phdr *phdr,
				   struct file *elf, bool is_interp,
				   struct arch_elf_state *state)
{
	 
	return 0;
}

static inline int arch_check_elf(struct elfhdr *ehdr, bool has_interp,
				 struct elfhdr *interp_ehdr,
				 struct arch_elf_state *state)
{
	 
	return 0;
}

#endif  

static inline int make_prot(u32 p_flags, struct arch_elf_state *arch_state,
			    bool has_interp, bool is_interp)
{
	int prot = 0;

	if (p_flags & PF_R)
		prot |= PROT_READ;
	if (p_flags & PF_W)
		prot |= PROT_WRITE;
	if (p_flags & PF_X)
		prot |= PROT_EXEC;

	return arch_elf_adjust_prot(prot, arch_state, has_interp, is_interp);
}


static int parse_elf_properties(struct file *f, const struct elf_phdr *phdr,
				struct arch_elf_state *arch)
{
	/* Stub: GNU properties not needed for minimal kernel */
	return 0;
}

static int load_elf_binary(struct linux_binprm *bprm)
{
	unsigned long load_bias = 0, phdr_addr = 0;
	int first_pt_load = 1;
	unsigned long error;
	struct elf_phdr *elf_ppnt, *elf_phdata;
	struct elf_phdr *elf_property_phdata = NULL;
	unsigned long elf_bss, elf_brk;
	int bss_prot = 0;
	int retval, i;
	unsigned long elf_entry;
	unsigned long e_entry;
	unsigned long start_code, end_code, start_data, end_data;
	unsigned long reloc_func_desc __maybe_unused = 0;
	int executable_stack = EXSTACK_DEFAULT;
	struct elfhdr *elf_ex = (struct elfhdr *)bprm->buf;
	struct arch_elf_state arch_state = INIT_ARCH_ELF_STATE;
	struct mm_struct *mm;
	struct pt_regs *regs;

	retval = -ENOEXEC;
	 
	if (memcmp(elf_ex->e_ident, ELFMAG, SELFMAG) != 0)
		goto out;

	if (elf_ex->e_type != ET_EXEC && elf_ex->e_type != ET_DYN)
		goto out;
	if (!elf_check_arch(elf_ex))
		goto out;
	if (elf_check_fdpic(elf_ex))
		goto out;
	if (!bprm->file->f_op->mmap)
		goto out;

	elf_phdata = load_elf_phdrs(elf_ex, bprm->file);
	if (!elf_phdata)
		goto out;

	/*
	 * The init ELF is a static ET_EXEC with no PT_INTERP, so no
	 * interpreter is ever loaded here; only record PT_GNU_PROPERTY.
	 */
	elf_ppnt = elf_phdata;
	for (i = 0; i < elf_ex->e_phnum; i++, elf_ppnt++) {
		if (elf_ppnt->p_type == PT_GNU_PROPERTY) {
			elf_property_phdata = elf_ppnt;
			break;
		}
	}

	elf_ppnt = elf_phdata;
	for (i = 0; i < elf_ex->e_phnum; i++, elf_ppnt++)
		switch (elf_ppnt->p_type) {
		case PT_GNU_STACK:
			if (elf_ppnt->p_flags & PF_X)
				executable_stack = EXSTACK_ENABLE_X;
			else
				executable_stack = EXSTACK_DISABLE_X;
			break;

		case PT_LOPROC ... PT_HIPROC:
			retval = arch_elf_pt_proc(elf_ex, elf_ppnt,
						  bprm->file, false,
						  &arch_state);
			if (retval)
				goto out_free_dentry;
			break;
		}

	retval = parse_elf_properties(bprm->file,
				      elf_property_phdata, &arch_state);
	if (retval)
		goto out_free_dentry;


	retval = arch_check_elf(elf_ex, false, NULL, &arch_state);
	if (retval)
		goto out_free_dentry;

	 
	retval = begin_new_exec(bprm);
	if (retval)
		goto out_free_dentry;

	 
	SET_PERSONALITY2(*elf_ex, &arch_state);
	if (elf_read_implies_exec(*elf_ex, executable_stack))
		current->personality |= READ_IMPLIES_EXEC;

	if (!(current->personality & ADDR_NO_RANDOMIZE) && randomize_va_space)
		current->flags |= PF_RANDOMIZE;

	setup_new_exec(bprm);

	 
	retval = setup_arg_pages(bprm, randomize_stack_top(STACK_TOP),
				 executable_stack);
	if (retval < 0)
		goto out_free_dentry;
	
	elf_bss = 0;
	elf_brk = 0;

	start_code = ~0UL;
	end_code = 0;
	start_data = 0;
	end_data = 0;

	 
	for(i = 0, elf_ppnt = elf_phdata;
	    i < elf_ex->e_phnum; i++, elf_ppnt++) {
		int elf_prot, elf_flags;
		unsigned long k, vaddr;
		unsigned long total_size = 0;

		if (elf_ppnt->p_type != PT_LOAD)
			continue;

		if (unlikely (elf_brk > elf_bss)) {
			unsigned long nbyte;
	            
			 
			retval = set_brk(elf_bss + load_bias,
					 elf_brk + load_bias,
					 bss_prot);
			if (retval)
				goto out_free_dentry;
			nbyte = ELF_PAGEOFFSET(elf_bss);
			if (nbyte) {
				nbyte = ELF_MIN_ALIGN - nbyte;
				if (nbyte > elf_brk - elf_bss)
					nbyte = elf_brk - elf_bss;
				if (clear_user((void __user *)elf_bss +
							load_bias, nbyte)) {
					 
				}
			}
		}

		elf_prot = make_prot(elf_ppnt->p_flags, &arch_state,
				     false, false);

		elf_flags = MAP_PRIVATE;

		vaddr = elf_ppnt->p_vaddr;

		if (!first_pt_load) {
			elf_flags |= MAP_FIXED;
		} else {
			/* init ELF is a static ET_EXEC; no ET_DYN/PIE here */
			elf_flags |= MAP_FIXED_NOREPLACE;
		}

		error = elf_map(bprm->file, load_bias + vaddr, elf_ppnt,
				elf_prot, elf_flags, total_size);
		if (BAD_ADDR(error)) {
			retval = IS_ERR((void *)error) ?
				PTR_ERR((void*)error) : -EINVAL;
			goto out_free_dentry;
		}

		if (first_pt_load)
			first_pt_load = 0;

		 
		if (elf_ppnt->p_offset <= elf_ex->e_phoff &&
		    elf_ex->e_phoff < elf_ppnt->p_offset + elf_ppnt->p_filesz) {
			phdr_addr = elf_ex->e_phoff - elf_ppnt->p_offset +
				    elf_ppnt->p_vaddr;
		}

		k = elf_ppnt->p_vaddr;
		if ((elf_ppnt->p_flags & PF_X) && k < start_code)
			start_code = k;
		if (start_data < k)
			start_data = k;

		 
		if (BAD_ADDR(k) || elf_ppnt->p_filesz > elf_ppnt->p_memsz ||
		    elf_ppnt->p_memsz > TASK_SIZE ||
		    TASK_SIZE - elf_ppnt->p_memsz < k) {
			 
			retval = -EINVAL;
			goto out_free_dentry;
		}

		k = elf_ppnt->p_vaddr + elf_ppnt->p_filesz;

		if (k > elf_bss)
			elf_bss = k;
		if ((elf_ppnt->p_flags & PF_X) && end_code < k)
			end_code = k;
		if (end_data < k)
			end_data = k;
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
	start_code += load_bias;
	end_code += load_bias;
	start_data += load_bias;
	end_data += load_bias;

	 
	retval = set_brk(elf_bss, elf_brk, bss_prot);
	if (retval)
		goto out_free_dentry;
	if (likely(elf_bss != elf_brk) && unlikely(padzero(elf_bss))) {
		retval = -EFAULT;  
		goto out_free_dentry;
	}

	elf_entry = e_entry;
	if (BAD_ADDR(elf_entry)) {
		retval = -EINVAL;
		goto out_free_dentry;
	}

	kfree(elf_phdata);

	set_binfmt(&elf_format);

#ifdef ARCH_HAS_SETUP_ADDITIONAL_PAGES
	retval = ARCH_SETUP_ADDITIONAL_PAGES(bprm, elf_ex, 0);
	if (retval < 0)
		goto out;
#endif  

	retval = create_elf_tables(bprm, elf_ex, 0,
				   e_entry, phdr_addr);
	if (retval < 0)
		goto out;

	mm = current->mm;
	mm->end_code = end_code;
	mm->start_code = start_code;
	mm->start_data = start_data;
	mm->end_data = end_data;
	mm->start_stack = bprm->p;

	if ((current->flags & PF_RANDOMIZE) && (randomize_va_space > 1)) {
		mm->brk = mm->start_brk = arch_randomize_brk(mm);
#ifdef compat_brk_randomized
		current->brk_randomized = 1;
#endif
	}

	if (current->personality & MMAP_PAGE_ZERO) {
		 
		error = vm_mmap(NULL, 0, PAGE_SIZE, PROT_READ | PROT_EXEC,
				MAP_FIXED | MAP_PRIVATE, 0);
	}

	regs = current_pt_regs();
#ifdef ELF_PLAT_INIT
	 
	ELF_PLAT_INIT(regs, reloc_func_desc);
#endif

	finalize_exec(bprm);
	START_THREAD(elf_ex, regs, elf_entry, bprm->p);
	retval = 0;
out:
	return retval;

out_free_dentry:
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

