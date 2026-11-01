// SPDX-License-Identifier: GPL-2.0
#ifndef __LINUX_KBUILD_H
#error "Please do not build this file directly, build asm-offsets.c instead"
#endif

void foo(void)
{
	OFFSET(PT_EAX, pt_regs, ax);
	OFFSET(PT_GS, pt_regs, gs);
	OFFSET(PT_ORIG_EAX, pt_regs, orig_ax);
}
