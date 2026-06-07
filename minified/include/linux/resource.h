#ifndef _LINUX_RESOURCE_H
#define _LINUX_RESOURCE_H

#include <linux/time.h>
#include <linux/types.h>

/* Inlined from uapi/linux/resource.h */
#define	RUSAGE_SELF	0
#define	RUSAGE_CHILDREN	(-1)
#define RUSAGE_BOTH	(-2)
#define	RUSAGE_THREAD	1

struct	rusage {
	struct __kernel_old_timeval ru_utime;
	struct __kernel_old_timeval ru_stime;
	__kernel_long_t	ru_maxrss;
	__kernel_long_t	ru_ixrss;
	__kernel_long_t	ru_idrss;
	__kernel_long_t	ru_isrss;
	__kernel_long_t	ru_minflt;
	__kernel_long_t	ru_majflt;
	__kernel_long_t	ru_nswap;
	__kernel_long_t	ru_inblock;
	__kernel_long_t	ru_oublock;
	__kernel_long_t	ru_msgsnd;
	__kernel_long_t	ru_msgrcv;
	__kernel_long_t	ru_nsignals;
	__kernel_long_t	ru_nvcsw;
	__kernel_long_t	ru_nivcsw;
};

struct rlimit {
	__kernel_ulong_t	rlim_cur;
	__kernel_ulong_t	rlim_max;
};

#define RLIM64_INFINITY		(~0ULL)

struct rlimit64 {
	__u64 rlim_cur;
	__u64 rlim_max;
};

#define	PRIO_MIN	(-20)
#define	PRIO_MAX	20
#define	PRIO_PROCESS	0
#define	PRIO_PGRP	1
#define	PRIO_USER	2
#define _STK_LIM	(8*1024*1024)
#define MLOCK_LIMIT	(8*1024*1024)

/* Inlined from asm-generic/resource.h */
#define RLIMIT_CPU		0
#define RLIMIT_FSIZE		1
#define RLIMIT_DATA		2
#define RLIMIT_STACK		3
#define RLIMIT_CORE		4

#ifndef RLIMIT_RSS
# define RLIMIT_RSS		5
#endif
#ifndef RLIMIT_NPROC
# define RLIMIT_NPROC		6
#endif
#ifndef RLIMIT_NOFILE
# define RLIMIT_NOFILE		7
#endif
#ifndef RLIMIT_MEMLOCK
# define RLIMIT_MEMLOCK		8
#endif
#ifndef RLIMIT_AS
# define RLIMIT_AS		9
#endif

#define RLIMIT_LOCKS		10
#define RLIMIT_SIGPENDING	11
#define RLIMIT_MSGQUEUE		12
#define RLIMIT_NICE		13
#define RLIMIT_RTPRIO		14
#define RLIMIT_RTTIME		15
#define RLIM_NLIMITS		16

#ifndef RLIM_INFINITY
# define RLIM_INFINITY		(~0UL)
#endif

#define MQ_BYTES_MAX	819200

#define INIT_RLIMITS							\
{									\
	[RLIMIT_CPU]		= {  RLIM_INFINITY,  RLIM_INFINITY },	\
	[RLIMIT_FSIZE]		= {  RLIM_INFINITY,  RLIM_INFINITY },	\
	[RLIMIT_DATA]		= {  RLIM_INFINITY,  RLIM_INFINITY },	\
	[RLIMIT_STACK]		= {       _STK_LIM,  RLIM_INFINITY },	\
	[RLIMIT_CORE]		= {              0,  RLIM_INFINITY },	\
	[RLIMIT_RSS]		= {  RLIM_INFINITY,  RLIM_INFINITY },	\
	[RLIMIT_NPROC]		= {              0,              0 },	\
	[RLIMIT_NOFILE]		= {   INR_OPEN_CUR,   INR_OPEN_MAX },	\
	[RLIMIT_MEMLOCK]	= {    MLOCK_LIMIT,    MLOCK_LIMIT },	\
	[RLIMIT_AS]		= {  RLIM_INFINITY,  RLIM_INFINITY },	\
	[RLIMIT_LOCKS]		= {  RLIM_INFINITY,  RLIM_INFINITY },	\
	[RLIMIT_SIGPENDING]	= { 		0,	       0 },	\
	[RLIMIT_MSGQUEUE]	= {   MQ_BYTES_MAX,   MQ_BYTES_MAX },	\
	[RLIMIT_NICE]		= { 0, 0 },				\
	[RLIMIT_RTPRIO]		= { 0, 0 },				\
	[RLIMIT_RTTIME]		= {  RLIM_INFINITY,  RLIM_INFINITY },	\
}

struct task_struct;

void getrusage(struct task_struct *p, int who, struct rusage *ru);

#endif
