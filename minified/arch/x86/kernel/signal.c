

/*
 * Userspace signal-frame delivery is unreachable in this minimal build: the
 * single static init never installs a handler nor receives a signal, so
 * setup_rt_frame()/sigreturn()/arch_do_signal_or_restart() are never invoked.
 * arch_do_signal_or_restart() falls back to the __weak no-op in
 * kernel/entry/common.c (gated on _TIF_SIGPENDING, which is never set here).
 *
 *
 * Referenced from ARCH_DLINFO (AT_MINSIGSTKSZ) in create_elf_tables() via
 * asm/elf.h.  No signal frame is ever built, so report a zero minimum.
 */
unsigned long get_sigframe_size(void) {
	return 0; }
