// SPDX-License-Identifier: GPL-2.0-only
/*
 * FPU signal-frame save/restore (copy_fpstate_to_sigframe, fpu__restore_sig,
 * fpu__alloc_mathframe, fpu__get_fpstate_size) was only reachable from
 * arch/x86/kernel/signal.c's userspace signal-delivery path, which is
 * unreachable in this minimal build (the static init never receives a signal).
 * The whole compilation unit is therefore empty.
 */
