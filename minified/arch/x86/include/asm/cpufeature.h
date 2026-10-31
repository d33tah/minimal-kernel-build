
#ifndef _ASM_X86_CPUFEATURE_H
#define _ASM_X86_CPUFEATURE_H

enum cpuid_leafs {
	CPUID_1_EDX = 0,
	CPUID_8000_0001_EDX,
	CPUID_8086_0001_EDX,
	CPUID_LNX_1,
	CPUID_1_ECX,
	CPUID_C000_0001_EDX,
	CPUID_8000_0001_ECX,
	CPUID_LNX_2,
	CPUID_LNX_3,
	CPUID_7_0_EBX,
};

#define test_cpu_cap(c, bit) \
	arch_test_bit(bit, (unsigned long *)((c)->x86_capability))

#define cpu_has(c, bit) test_cpu_cap(c, bit)

#define boot_cpu_has(bit) cpu_has(&boot_cpu_data, bit)

#define static_cpu_has(bit) boot_cpu_has(bit)

#endif
