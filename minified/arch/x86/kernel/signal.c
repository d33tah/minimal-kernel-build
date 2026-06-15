
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/init.h>

/*
 * Userspace signal-frame delivery is unreachable in this minimal build: the
 * single static init never installs a handler nor receives a signal, so
 * setup_rt_frame()/sigreturn()/arch_do_signal_or_restart() are never invoked.
 * arch_do_signal_or_restart() falls back to the __weak no-op in
 * kernel/entry/common.c (gated on _TIF_SIGPENDING, which is never set here).
 *
 * Only init_sigframe_size() must survive: it is called unconditionally at boot
 * from identify_cpu() in arch/x86/kernel/cpu/common.c.
 */
void __init init_sigframe_size(void)
{
}

/*
 * Referenced from ARCH_DLINFO (AT_MINSIGSTKSZ) in create_elf_tables() via
 * asm/elf.h.  No signal frame is ever built, so report a zero minimum.
 */
unsigned long get_sigframe_size(void)
{
	return 0;
}
