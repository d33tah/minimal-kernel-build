#include <linux/module.h>
static inline void iret_to_self(void)
{
	asm volatile("pushfl\n\t"
		     "pushl %%cs\n\t"
		     "pushl $1f\n\t"
		     "iret\n\t"
		     "1:"
		     : ASM_CALL_CONSTRAINT
		     :
		     : "memory");
}
static inline void sync_core(void)
{
	if (static_cpu_has(X86_FEATURE_SERIALIZE)) {
		serialize();
		return;
	}
	iret_to_self();
}
/* end sync_core.h */
extern void text_poke_early(void *addr, const void *opcode, size_t len);
extern int after_bootmem;

int __read_mostly alternatives_patched;

void text_poke_early(void *addr, const void *opcode, size_t len);

void __init_or_module text_poke_early(void *addr, const void *opcode,
				      size_t len)
{
	unsigned long flags;

	if (len > sizeof(long))
		len = sizeof(long);

	local_irq_save(flags);
	memcpy(addr, opcode, len);
	local_irq_restore(flags);
	sync_core();
}
