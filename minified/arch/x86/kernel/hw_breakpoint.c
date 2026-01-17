/* Stub hardware breakpoints - just cpu_dr7 needed for hw_breakpoint_active() */
#include <linux/percpu.h>

DEFINE_PER_CPU(unsigned long, cpu_dr7);

/* flush_ptrace_hw_breakpoint removed - calls removed from exit.c and process.c */
