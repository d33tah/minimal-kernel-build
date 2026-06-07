#ifndef _ASM_X86_PROCESSOR_FLAGS_H
#define _ASM_X86_PROCESSOR_FLAGS_H
#include <linux/const.h>

#define X86_EFLAGS_CF		_BITUL(0)
#define X86_EFLAGS_FIXED	_BITUL(1)
#define X86_EFLAGS_IF		_BITUL(9)
#define X86_EFLAGS_AC		_BITUL(18)
#define X86_EFLAGS_ID		_BITUL(21)

#define X86_CR0_PE		_BITUL(0)
#define X86_CR0_MP		_BITUL(1)
#define X86_CR0_EM		_BITUL(2)
#define X86_CR0_TS		_BITUL(3)
#define X86_CR0_ET		_BITUL(4)
#define X86_CR0_NE		_BITUL(5)
#define X86_CR0_WP		_BITUL(16)
#define X86_CR0_AM		_BITUL(18)
#define X86_CR0_PG		_BITUL(31)

#define X86_CR4_VME		_BITUL(0)
#define X86_CR4_PVI		_BITUL(1)
#define X86_CR4_TSD		_BITUL(2)
#define X86_CR4_DE		_BITUL(3)
#define X86_CR4_PSE		_BITUL(4)
#define X86_CR4_PAE		_BITUL(5)
#define X86_CR4_PGE		_BITUL(7)
#define X86_CR4_PCE		_BITUL(8)
#define X86_CR4_OSFXSR		_BITUL(9)
#define X86_CR4_OSXMMEXCPT	_BITUL(10)
#define X86_CR4_UMIP		_BITUL(11)
#define X86_CR4_FSGSBASE	_BITUL(16)
#define X86_CR4_PCIDE		_BITUL(17)
#define X86_CR4_SMEP		_BITUL(20)
#define X86_CR4_SMAP		_BITUL(21)
#define X86_CR4_CET		_BITUL(23)

#define CR0_STATE	(X86_CR0_PE | X86_CR0_MP | X86_CR0_ET | \
			 X86_CR0_NE | X86_CR0_WP | X86_CR0_AM | \
			 X86_CR0_PG)
#include <linux/mem_encrypt.h>
#define X86_VM_MASK 0
#define CR3_ADDR_MASK 0xFFFFFFFFull
#endif
