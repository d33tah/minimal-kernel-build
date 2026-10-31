
#ifndef _ASM_X86_CPUFEATURE_H
#define _ASM_X86_CPUFEATURE_H

#if defined(__KERNEL__) && !defined(__ASSEMBLY__)

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
	CPUID_D_1_EAX,
	CPUID_LNX_4,
	CPUID_7_1_EAX,
	CPUID_8000_0008_EBX,
	CPUID_6_EAX,
	CPUID_8000_000A_EDX,
	CPUID_7_ECX,
	CPUID_8000_0007_EBX,
	CPUID_7_EDX,
	CPUID_8000_001F_EAX,
};

#define test_cpu_cap(c, bit) \
	arch_test_bit(bit, (unsigned long *)((c)->x86_capability))

#define CHECK_BIT_IN_MASK_WORD(maskname, word, bit) \
	(((bit) >> 5) == (word) && (1UL << ((bit) & 31) & maskname##word))

#define REQUIRED_MASK_BIT_SET(feature_bit)                         \
	(CHECK_BIT_IN_MASK_WORD(REQUIRED_MASK, 0, feature_bit) ||  \
	 CHECK_BIT_IN_MASK_WORD(REQUIRED_MASK, 1, feature_bit) ||  \
	 CHECK_BIT_IN_MASK_WORD(REQUIRED_MASK, 2, feature_bit) ||  \
	 CHECK_BIT_IN_MASK_WORD(REQUIRED_MASK, 3, feature_bit) ||  \
	 CHECK_BIT_IN_MASK_WORD(REQUIRED_MASK, 4, feature_bit) ||  \
	 CHECK_BIT_IN_MASK_WORD(REQUIRED_MASK, 5, feature_bit) ||  \
	 CHECK_BIT_IN_MASK_WORD(REQUIRED_MASK, 6, feature_bit) ||  \
	 CHECK_BIT_IN_MASK_WORD(REQUIRED_MASK, 7, feature_bit) ||  \
	 CHECK_BIT_IN_MASK_WORD(REQUIRED_MASK, 8, feature_bit) ||  \
	 CHECK_BIT_IN_MASK_WORD(REQUIRED_MASK, 9, feature_bit) ||  \
	 CHECK_BIT_IN_MASK_WORD(REQUIRED_MASK, 10, feature_bit) || \
	 CHECK_BIT_IN_MASK_WORD(REQUIRED_MASK, 11, feature_bit) || \
	 CHECK_BIT_IN_MASK_WORD(REQUIRED_MASK, 12, feature_bit) || \
	 CHECK_BIT_IN_MASK_WORD(REQUIRED_MASK, 13, feature_bit) || \
	 CHECK_BIT_IN_MASK_WORD(REQUIRED_MASK, 14, feature_bit) || \
	 CHECK_BIT_IN_MASK_WORD(REQUIRED_MASK, 15, feature_bit) || \
	 CHECK_BIT_IN_MASK_WORD(REQUIRED_MASK, 16, feature_bit) || \
	 CHECK_BIT_IN_MASK_WORD(REQUIRED_MASK, 17, feature_bit) || \
	 CHECK_BIT_IN_MASK_WORD(REQUIRED_MASK, 18, feature_bit) || \
	 CHECK_BIT_IN_MASK_WORD(REQUIRED_MASK, 19, feature_bit) || \
	 REQUIRED_MASK_CHECK || BUILD_BUG_ON_ZERO(NCAPINTS != 20))

#define DISABLED_MASK_BIT_SET(feature_bit)                         \
	(CHECK_BIT_IN_MASK_WORD(DISABLED_MASK, 0, feature_bit) ||  \
	 CHECK_BIT_IN_MASK_WORD(DISABLED_MASK, 1, feature_bit) ||  \
	 CHECK_BIT_IN_MASK_WORD(DISABLED_MASK, 2, feature_bit) ||  \
	 CHECK_BIT_IN_MASK_WORD(DISABLED_MASK, 3, feature_bit) ||  \
	 CHECK_BIT_IN_MASK_WORD(DISABLED_MASK, 4, feature_bit) ||  \
	 CHECK_BIT_IN_MASK_WORD(DISABLED_MASK, 5, feature_bit) ||  \
	 CHECK_BIT_IN_MASK_WORD(DISABLED_MASK, 6, feature_bit) ||  \
	 CHECK_BIT_IN_MASK_WORD(DISABLED_MASK, 7, feature_bit) ||  \
	 CHECK_BIT_IN_MASK_WORD(DISABLED_MASK, 8, feature_bit) ||  \
	 CHECK_BIT_IN_MASK_WORD(DISABLED_MASK, 9, feature_bit) ||  \
	 CHECK_BIT_IN_MASK_WORD(DISABLED_MASK, 10, feature_bit) || \
	 CHECK_BIT_IN_MASK_WORD(DISABLED_MASK, 11, feature_bit) || \
	 CHECK_BIT_IN_MASK_WORD(DISABLED_MASK, 12, feature_bit) || \
	 CHECK_BIT_IN_MASK_WORD(DISABLED_MASK, 13, feature_bit) || \
	 CHECK_BIT_IN_MASK_WORD(DISABLED_MASK, 14, feature_bit) || \
	 CHECK_BIT_IN_MASK_WORD(DISABLED_MASK, 15, feature_bit) || \
	 CHECK_BIT_IN_MASK_WORD(DISABLED_MASK, 16, feature_bit) || \
	 CHECK_BIT_IN_MASK_WORD(DISABLED_MASK, 17, feature_bit) || \
	 CHECK_BIT_IN_MASK_WORD(DISABLED_MASK, 18, feature_bit) || \
	 CHECK_BIT_IN_MASK_WORD(DISABLED_MASK, 19, feature_bit) || \
	 DISABLED_MASK_CHECK || BUILD_BUG_ON_ZERO(NCAPINTS != 20))

#define cpu_has(c, bit)                                            \
	(__builtin_constant_p(bit) && REQUIRED_MASK_BIT_SET(bit) ? \
		 1 :                                               \
		 test_cpu_cap(c, bit))

#define cpu_feature_enabled(bit)                                   \
	(__builtin_constant_p(bit) && DISABLED_MASK_BIT_SET(bit) ? \
		 0 :                                               \
		 static_cpu_has(bit))

#define boot_cpu_has(bit) cpu_has(&boot_cpu_data, bit)

#define static_cpu_has(bit) boot_cpu_has(bit)

#endif
#endif
