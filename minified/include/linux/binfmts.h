#ifndef _LINUX_BINFMTS_H
#define _LINUX_BINFMTS_H

#include <linux/sched.h>

#define MAX_ARG_STRLEN (PAGE_SIZE * 32)
#define BINPRM_BUF_SIZE 256

struct linux_binprm {
	struct vm_area_struct *vma;
	struct mm_struct *mm;
	unsigned long p;
	unsigned long argmin;
	unsigned int point_of_no_return:1;
	struct file *file;
	struct cred *cred;
	int argc, envc;
	const char *filename;
	unsigned long exec;
	struct rlimit rlim_stack;
	char buf[BINPRM_BUF_SIZE];
} __randomize_layout;

struct linux_binfmt {
	struct module *module;
	int (*load_binary)(struct linux_binprm *);
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
