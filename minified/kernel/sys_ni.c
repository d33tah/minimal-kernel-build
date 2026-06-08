
#include <linux/linkage.h>
#include <linux/errno.h>

#include <asm/unistd.h>

#include <asm/syscall_wrapper.h>

asmlinkage long sys_ni_syscall(void);

asmlinkage long sys_ni_syscall(void)
{
	return -ENOSYS;
}

#ifndef COND_SYSCALL
#define COND_SYSCALL(name) cond_syscall(sys_##name)
#endif  

#ifndef COND_SYSCALL_COMPAT
#define COND_SYSCALL_COMPAT(name) cond_syscall(compat_sys_##name)
#endif  


COND_SYSCALL(io_setup);
COND_SYSCALL_COMPAT(io_setup);
COND_SYSCALL(io_destroy);
COND_SYSCALL(io_submit);
COND_SYSCALL_COMPAT(io_submit);
COND_SYSCALL(io_cancel);
COND_SYSCALL(io_getevents_time32);
COND_SYSCALL(io_getevents);
COND_SYSCALL(io_pgetevents_time32);
COND_SYSCALL(io_pgetevents);
COND_SYSCALL_COMPAT(io_pgetevents_time32);
COND_SYSCALL_COMPAT(io_pgetevents);



COND_SYSCALL(lookup_dcookie);
COND_SYSCALL_COMPAT(lookup_dcookie);

COND_SYSCALL(eventfd2);

COND_SYSCALL(epoll_create1);
COND_SYSCALL(epoll_ctl);
COND_SYSCALL(epoll_pwait);
COND_SYSCALL_COMPAT(epoll_pwait);
COND_SYSCALL(epoll_pwait2);
COND_SYSCALL_COMPAT(epoll_pwait2);




COND_SYSCALL(ioprio_set);
COND_SYSCALL(ioprio_get);

COND_SYSCALL(flock);











COND_SYSCALL(signalfd4);
COND_SYSCALL_COMPAT(signalfd4);




COND_SYSCALL(timerfd_create);
COND_SYSCALL(timerfd_settime);
COND_SYSCALL(timerfd_settime32);
COND_SYSCALL(timerfd_gettime);
COND_SYSCALL(timerfd_gettime32);


COND_SYSCALL(acct);

COND_SYSCALL(capget);
COND_SYSCALL(capset);



COND_SYSCALL(clone3);

COND_SYSCALL(futex);
COND_SYSCALL(futex_time32);
COND_SYSCALL(set_robust_list);
COND_SYSCALL_COMPAT(set_robust_list);
COND_SYSCALL(get_robust_list);
COND_SYSCALL_COMPAT(get_robust_list);
COND_SYSCALL(futex_waitv);



COND_SYSCALL(init_module);
COND_SYSCALL(delete_module);


COND_SYSCALL(syslog);








COND_SYSCALL(socket);
COND_SYSCALL(socketpair);
COND_SYSCALL(bind);
COND_SYSCALL(listen);
COND_SYSCALL(accept);
COND_SYSCALL(connect);
COND_SYSCALL(getsockname);
COND_SYSCALL(getpeername);
COND_SYSCALL(setsockopt);
COND_SYSCALL_COMPAT(setsockopt);
COND_SYSCALL(getsockopt);
COND_SYSCALL_COMPAT(getsockopt);
COND_SYSCALL(sendto);
COND_SYSCALL(shutdown);
COND_SYSCALL(recvfrom);
COND_SYSCALL_COMPAT(recvfrom);
COND_SYSCALL(sendmsg);
COND_SYSCALL_COMPAT(sendmsg);
COND_SYSCALL(recvmsg);
COND_SYSCALL_COMPAT(recvmsg);


COND_SYSCALL(mremap);


COND_SYSCALL(landlock_create_ruleset);
COND_SYSCALL(landlock_add_rule);
COND_SYSCALL(landlock_restrict_self);


COND_SYSCALL(fadvise64_64);

COND_SYSCALL(swapon);
COND_SYSCALL(swapoff);
COND_SYSCALL(mprotect);
COND_SYSCALL(msync);
COND_SYSCALL(mlock);
COND_SYSCALL(munlock);
COND_SYSCALL(mlockall);
COND_SYSCALL(munlockall);
COND_SYSCALL(mincore);
COND_SYSCALL(madvise);
COND_SYSCALL(process_madvise);
COND_SYSCALL(process_mrelease);
COND_SYSCALL(remap_file_pages);

COND_SYSCALL(accept4);
COND_SYSCALL(recvmmsg);
COND_SYSCALL(recvmmsg_time32);
COND_SYSCALL_COMPAT(recvmmsg_time32);
COND_SYSCALL_COMPAT(recvmmsg_time64);



COND_SYSCALL(name_to_handle_at);
COND_SYSCALL(open_by_handle_at);
COND_SYSCALL_COMPAT(open_by_handle_at);

COND_SYSCALL(sendmmsg);
COND_SYSCALL_COMPAT(sendmmsg);

COND_SYSCALL(kcmp);

COND_SYSCALL(finit_module);

COND_SYSCALL(seccomp);

COND_SYSCALL(memfd_create);


COND_SYSCALL(execveat);

COND_SYSCALL(userfaultfd);

COND_SYSCALL(membarrier);

COND_SYSCALL(mlock2);

COND_SYSCALL(copy_file_range);

COND_SYSCALL(pkey_mprotect);
COND_SYSCALL(pkey_alloc);
COND_SYSCALL(pkey_free);

COND_SYSCALL(memfd_secret);


COND_SYSCALL(pciconfig_read);
COND_SYSCALL(pciconfig_write);
COND_SYSCALL(pciconfig_iobase);

COND_SYSCALL(socketcall);
COND_SYSCALL_COMPAT(socketcall);


COND_SYSCALL(vm86old);
COND_SYSCALL(modify_ldt);
COND_SYSCALL(vm86);
COND_SYSCALL(kexec_file_load);

COND_SYSCALL(s390_pci_mmio_read);
COND_SYSCALL(s390_pci_mmio_write);
COND_SYSCALL(s390_ipc);
COND_SYSCALL_COMPAT(s390_ipc);

COND_SYSCALL(rtas);
COND_SYSCALL(spu_run);
COND_SYSCALL(spu_create);
COND_SYSCALL(subpage_prot);



COND_SYSCALL(epoll_create);
COND_SYSCALL(eventfd);
COND_SYSCALL(signalfd);
COND_SYSCALL_COMPAT(signalfd);

COND_SYSCALL(fadvise64);

COND_SYSCALL(epoll_wait);
COND_SYSCALL(recv);
COND_SYSCALL_COMPAT(recv);
COND_SYSCALL(send);
COND_SYSCALL(uselib);

COND_SYSCALL(time32);
COND_SYSCALL(stime32);
COND_SYSCALL(utime32);
COND_SYSCALL(adjtimex_time32);
COND_SYSCALL(sched_rr_get_interval_time32);
COND_SYSCALL(nanosleep_time32);
COND_SYSCALL(rt_sigtimedwait_time32);
COND_SYSCALL_COMPAT(rt_sigtimedwait_time32);
COND_SYSCALL(timer_settime32);
COND_SYSCALL(timer_gettime32);
COND_SYSCALL(clock_settime32);
COND_SYSCALL(clock_gettime32);
COND_SYSCALL(clock_getres_time32);
COND_SYSCALL(clock_nanosleep_time32);
COND_SYSCALL(utimes_time32);
COND_SYSCALL(futimesat_time32);
COND_SYSCALL(pselect6_time32);
COND_SYSCALL_COMPAT(pselect6_time32);
COND_SYSCALL(ppoll_time32);
COND_SYSCALL_COMPAT(ppoll_time32);
COND_SYSCALL(utimensat_time32);
COND_SYSCALL(clock_adjtime32);


COND_SYSCALL(sgetmask);
COND_SYSCALL(ssetmask);

COND_SYSCALL(sysfs);

COND_SYSCALL(rseq);
