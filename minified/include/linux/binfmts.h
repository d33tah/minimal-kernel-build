#ifndef _LINUX_BINFMTS_H
#define _LINUX_BINFMTS_H

#include <linux/sched.h>

#define MAX_ARG_STRLEN (PAGE_SIZE * 32)
#define BINPRM_BUF_SIZE 256

struct linux_binprm {
	struct vm_area_struct *vma;
	unsigned long vma_pages;
	struct mm_struct *mm;
	unsigned long p;  
	unsigned long argmin;  
	unsigned int
		 
		have_execfd:1,

		execfd_creds:1,
		 
		secureexec:1,
		point_of_no_return:1;
	struct file *executable;  
	struct file *interpreter;
	struct file *file;
	struct cred *cred;	 
	int unsafe;		 
	unsigned int per_clear;	 
	int argc, envc;
	const char *filename;	 
	const char *interp;	 
	const char *fdpath;	 
	unsigned interp_flags;
	int execfd;		 
	unsigned long loader, exec;

	struct rlimit rlim_stack;  

	char buf[BINPRM_BUF_SIZE];
} __randomize_layout;

struct linux_binfmt {
	struct list_head lh;
	struct module *module;
	int (*load_binary)(struct linux_binprm *);
	int (*load_shlib)(struct file *);
} __randomize_layout;

extern void __register_binfmt(struct linux_binfmt *fmt);

static inline void register_binfmt(struct linux_binfmt *fmt)
{
	__register_binfmt(fmt);
}

extern int begin_new_exec(struct linux_binprm * bprm);
extern void setup_new_exec(struct linux_binprm * bprm);

#define EXSTACK_DEFAULT   0	 
#define EXSTACK_DISABLE_X 1	 
#define EXSTACK_ENABLE_X  2	 

extern int setup_arg_pages(struct linux_binprm * bprm,
			   unsigned long stack_top,
			   int executable_stack);
extern void set_binfmt(struct linux_binfmt *new);

int kernel_execve(const char *filename,
		  const char *const *argv, const char *const *envp);

#endif  
