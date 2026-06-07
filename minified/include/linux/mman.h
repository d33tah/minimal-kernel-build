#ifndef _LINUX_MMAN_H
#define _LINUX_MMAN_H

#include <linux/mm.h>
#include <linux/percpu_counter.h>

#include <linux/atomic.h>
/* inlined from asm/mman.h */
#define PROT_READ	0x1
#define PROT_WRITE	0x2
#define PROT_EXEC	0x4
#define MAP_FIXED	0x10
#define MAP_FIXED_NOREPLACE	0x100000
#define MAP_GROWSDOWN	0x0100

#define MAP_SHARED	0x01
#define MAP_PRIVATE	0x02

/* sysctl_overcommit_* externs already in mm.h */
extern struct percpu_counter vm_committed_as;

static inline void vm_acct_memory(long pages)
{
	percpu_counter_add_batch(&vm_committed_as, pages, 0);
}

static inline void vm_unacct_memory(long pages)
{
	vm_acct_memory(-pages);
}

#ifndef arch_calc_vm_prot_bits
#define arch_calc_vm_prot_bits(prot, pkey) 0
#endif

#ifndef arch_calc_vm_flag_bits
#define arch_calc_vm_flag_bits(flags) 0
#endif

#define _calc_vm_trans(x, bit1, bit2) \
  ((!(bit1) || !(bit2)) ? 0 : \
  ((bit1) <= (bit2) ? ((x) & (bit1)) * ((bit2) / (bit1)) \
   : ((x) & (bit1)) / ((bit1) / (bit2))))

static inline unsigned long
calc_vm_prot_bits(unsigned long prot, unsigned long pkey)
{
	return _calc_vm_trans(prot, PROT_READ,  VM_READ ) |
	       _calc_vm_trans(prot, PROT_WRITE, VM_WRITE) |
	       _calc_vm_trans(prot, PROT_EXEC,  VM_EXEC) |
	       arch_calc_vm_prot_bits(prot, pkey);
}

static inline unsigned long
calc_vm_flag_bits(unsigned long flags)
{
	return _calc_vm_trans(flags, MAP_GROWSDOWN,  VM_GROWSDOWN ) |
	       arch_calc_vm_flag_bits(flags);
}

#endif  
