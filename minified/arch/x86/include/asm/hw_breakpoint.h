/* --- 2025-12-06 13:40 --- uapi/asm/hw_breakpoint.h removed (empty file) */
#ifndef	_I386_HW_BREAKPOINT_H
#define	_I386_HW_BREAKPOINT_H

#define	__ARCH_HW_BREAKPOINT_H

 
struct arch_hw_breakpoint {
	unsigned long	address;
	unsigned long	mask;
	u8		len;
	u8		type;
};

#include <linux/kdebug.h>
#include <linux/percpu.h>
#include <linux/list.h>

 
#define X86_BREAKPOINT_LEN_X		0x40
#define X86_BREAKPOINT_LEN_1		0x40
#define X86_BREAKPOINT_LEN_2		0x44
#define X86_BREAKPOINT_LEN_4		0x4c


 

 
#define X86_BREAKPOINT_EXECUTE	0x80
 
#define X86_BREAKPOINT_WRITE	0x81
 
#define X86_BREAKPOINT_RW	0x83

 
#define HBP_NUM 4


struct perf_event_attr;
struct perf_event;
struct pmu;

#endif
