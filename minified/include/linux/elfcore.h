#ifndef _LINUX_ELFCORE_H
#define _LINUX_ELFCORE_H

#include <asm/user.h> /* linux/user.h redirect */
#include <linux/bug.h>
#include <linux/sched/task_stack.h>
#include <linux/types.h>
#include <linux/signal.h>
#include <linux/time.h>
#include <linux/ptrace.h>
#include <linux/fs.h>
#include <linux/elf.h>

struct coredump_params;

struct elf_siginfo
{
	int	si_signo;			 
	int	si_code;			 
	int	si_errno;			 
};

struct elf_prstatus_common
{
	struct elf_siginfo pr_info;	 
	short	pr_cursig;		 
	unsigned long pr_sigpend;	 
	unsigned long pr_sighold;	 
	pid_t	pr_pid;
	pid_t	pr_ppid;
	pid_t	pr_pgrp;
	pid_t	pr_sid;
	struct __kernel_old_timeval pr_utime;	 
	struct __kernel_old_timeval pr_stime;	 
	struct __kernel_old_timeval pr_cutime;	 
	struct __kernel_old_timeval pr_cstime;	 
};

struct elf_prstatus
{
	struct elf_prstatus_common common;
	elf_gregset_t pr_reg;	 
	int pr_fpvalid;		 
};

#define ELF_PRARGSZ	(80)	 

struct elf_prpsinfo
{
	char	pr_state;	 
	char	pr_sname;	 
	char	pr_zomb;	 
	char	pr_nice;	 
	unsigned long pr_flag;	 
	__kernel_uid_t	pr_uid;
	__kernel_gid_t	pr_gid;
	pid_t	pr_pid, pr_ppid, pr_pgrp, pr_sid;
	 
	 
	char	pr_fname[16];
	char	pr_psargs[ELF_PRARGSZ];
};

extern int dump_fpu (struct pt_regs *, elf_fpregset_t *);

#endif  
