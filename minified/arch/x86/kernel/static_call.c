// SPDX-License-Identifier: GPL-2.0
#include <linux/static_call.h>
#include <asm/cpufeatures.h>
#include <linux/memory.h>
#include <asm/text-patching.h>

#include "asm-generic/int-ll64.h"
#include "asm/bug.h"
#include "asm/cpufeature.h"
#include "asm/cpufeatures.h"
#include "asm/nops.h"
#include "asm/nospec-branch.h"
#include "asm/string_32.h"
#include "linux/init.h"
#include "linux/kconfig.h"
#include "linux/kernel.h"
#include "linux/mutex.h"
#include "linux/printk.h"
#include "linux/types.h"

enum insn_type {
	CALL = 0, /* site call */
	NOP = 1,  /* site cond-call */
	JMP = 2,  /* tramp / site tail-call */
	RET = 3,  /* tramp / site cond-tail-call */
};

/*
 * ud1 %esp, %ecx - a 3 byte #UD that is unique to trampolines, chosen such
 * that there is no false-positive trampoline identification while also being a
 * speculation stop.
 */
static const u8 tramp_ud[] = { 0x0f, 0xb9, 0xcc };

/*
 * cs cs cs xorl %eax, %eax - a single 5 byte instruction that clears %[er]ax
 */
static const u8 xor5rax[] = { 0x2e, 0x2e, 0x2e, 0x31, 0xc0 };

static const u8 retinsn[] = { RET_INSN_OPCODE, 0xcc, 0xcc, 0xcc, 0xcc };

static void __ref __static_call_transform(void *insn, enum insn_type type,
					  void *func, bool modinit)
{
	const void *emulate = NULL;
	int size = CALL_INSN_SIZE;
	const void *code;

	switch (type) {
	case CALL:
		code = text_gen_insn(CALL_INSN_OPCODE, insn, func);
		if (func == &__static_call_return0) {
			emulate = code;
			code = &xor5rax;
		}

		break;

	case NOP:
		code = x86_nops[5];
		break;

	case JMP:
		code = text_gen_insn(JMP32_INSN_OPCODE, insn, func);
		break;

	case RET:
		if (cpu_feature_enabled(X86_FEATURE_RETHUNK))
			code = text_gen_insn(JMP32_INSN_OPCODE, insn, &__x86_return_thunk);
		else
			code = &retinsn;
		break;
	}

	if (memcmp(insn, code, size) == 0)
		return;

	if (system_state == SYSTEM_BOOTING || modinit)
		return text_poke_early(insn, code, size);

	text_poke_bp(insn, code, size, emulate);
}

static void __static_call_validate(void *insn, bool tail, bool tramp)
{
	u8 opcode = *(u8 *)insn;

	if (tramp && memcmp(insn+5, tramp_ud, 3)) {
		pr_err("trampoline signature fail");
		BUG();
	}

	if (tail) {
		if (opcode == JMP32_INSN_OPCODE ||
		    opcode == RET_INSN_OPCODE)
			return;
	} else {
		if (opcode == CALL_INSN_OPCODE ||
		    !memcmp(insn, x86_nops[5], 5) ||
		    !memcmp(insn, xor5rax, 5))
			return;
	}

	/*
	 * If we ever trigger this, our text is corrupt, we'll probably not live long.
	 */
	pr_err("unexpected static_call insn opcode 0x%x at %pS\n", opcode, insn);
	BUG();
}

static inline enum insn_type __sc_insn(bool null, bool tail)
{
	/*
	 * Encode the following table without branches:
	 *
	 *	tail	null	insn
	 *	-----+-------+------
	 *	  0  |   0   |  CALL
	 *	  0  |   1   |  NOP
	 *	  1  |   0   |  JMP
	 *	  1  |   1   |  RET
	 */
	return 2*tail + null;
}

void arch_static_call_transform(void *site, void *tramp, void *func, bool tail)
{
	mutex_lock(&text_mutex);

	if (tramp) {
		__static_call_validate(tramp, true, true);
		__static_call_transform(tramp, __sc_insn(!func, true), func, false);
	}

	if (IS_ENABLED(CONFIG_HAVE_STATIC_CALL_INLINE) && site) {
		__static_call_validate(site, tail, false);
		__static_call_transform(site, __sc_insn(!func, tail), func, false);
	}

	mutex_unlock(&text_mutex);
}
EXPORT_SYMBOL_GPL(arch_static_call_transform);

