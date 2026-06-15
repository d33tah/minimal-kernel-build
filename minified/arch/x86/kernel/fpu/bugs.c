#include <asm/fpu/api.h>

void __init fpu__init_check_bugs(void)
{
	/*
	 * The only work here was the Pentium FDIV-bug probe, whose sole effect was
	 * set_cpu_bug(X86_BUG_FDIV). That bug bit is never read anywhere in this
	 * tree, so the whole FPU self-test was dead.
	 */
}
