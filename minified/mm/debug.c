 
 

#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/trace_events.h>
#include <linux/memcontrol.h>
#include <linux/migrate.h>
#include <linux/page_owner.h>
#include <linux/ctype.h>

#include "internal.h"

 
#undef EM
#undef EMe
#define EM(a, b)	b,
#define EMe(a, b)	b

#define MIGRATE_REASON \
	EM(MR_COMPACTION, "compaction") \
	EM(MR_MEMORY_FAILURE, "memory_failure") \
	EM(MR_MEMORY_HOTPLUG, "memory_hotplug") \
	EM(MR_SYSCALL, "syscall") \
	EM(MR_MEMPOLICY_MBIND, "mempolicy_mbind") \
	EM(MR_NUMA_MISPLACED, "numa_misplaced") \
	EM(MR_CONTIG_RANGE, "contig_range") \
	EM(MR_LONGTERM_PIN, "longterm_pin") \
	EM(MR_DEMOTION, "demotion") \
	EMe(MR_TYPES, "unknown")

const char *migrate_reason_names[MR_TYPES] = {
	MIGRATE_REASON
};

const struct trace_print_flags pageflag_names[] = {
	{0, NULL}
};

const struct trace_print_flags gfpflag_names[] = {
	{0, NULL}
};

const struct trace_print_flags vmaflag_names[] = {
	{0, NULL}
};

void dump_page(struct page *page, const char *reason)
{
	 
}

