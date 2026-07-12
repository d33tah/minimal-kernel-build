#ifndef _LINUX_SIGNAL_H
#define _LINUX_SIGNAL_H

#include <linux/bug.h>
#include <linux/signal_types.h>
#include <linux/string.h>

struct task_struct;

/* Removed: print_fatal_signals - never used */

static inline void clear_siginfo(kernel_siginfo_t *info) {
	memset(info, 0, sizeof(*info)); }

#ifndef __HAVE_ARCH_SIG_SETOPS

static inline void sigemptyset(sigset_t *set) {
	switch (_NSIG_WORDS) {
	default:
		memset(set, 0, sizeof(sigset_t));
		break;
	case 2: set->sig[1] = 0;
		fallthrough;
	case 1:	set->sig[0] = 0; } }




#endif

static inline void init_sigpending(struct sigpending *sig) {
	sigemptyset(&sig->signal);
	INIT_LIST_HEAD(&sig->list); }

extern void exit_signals(struct task_struct *tsk);

extern struct kmem_cache *sighand_cachep;

void signals_init(void);


#endif  
