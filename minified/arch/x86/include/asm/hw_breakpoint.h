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


/* All hw_breakpoint functions removed - PERF_EVENTS disabled:
   arch_check_bp_in_kernelspace, hw_breakpoint_arch_parse,
   hw_breakpoint_exceptions_notify, arch_install_hw_breakpoint,
   arch_uninstall_hw_breakpoint, hw_breakpoint_pmu_read,
   hw_breakpoint_pmu_unthrottle, arch_fill_perf_breakpoint,
   encode_dr7, decode_dr7, arch_bp_generic_fields, perf_ops_bp */

#endif	 
