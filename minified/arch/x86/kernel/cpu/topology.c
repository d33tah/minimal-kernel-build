 
 

#include <linux/cpu.h>
#include <asm/apic.h>
#include <asm/memtype.h>
#include <asm/processor.h>

#include "cpu.h"

 
#define SMT_LEVEL	0

 
#define INVALID_TYPE	0
#define SMT_TYPE	1
#define CORE_TYPE	2
#define DIE_TYPE	5

#define LEAFB_SUBTYPE(ecx)		(((ecx) >> 8) & 0xff)
#define BITS_SHIFT_NEXT_LEVEL(eax)	((eax) & 0x1f)
#define LEVEL_MAX_SIBLINGS(ebx)		((ebx) & 0xffff)

unsigned int __max_die_per_package __read_mostly = 1;


int detect_extended_topology_early(struct cpuinfo_x86 *c)
{
	return 0;
}

 
int detect_extended_topology(struct cpuinfo_x86 *c)
{
	return 0;
}
