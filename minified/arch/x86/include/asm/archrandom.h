 
 

#ifndef ASM_X86_ARCHRANDOM_H
#define ASM_X86_ARCHRANDOM_H

#include <asm/processor.h>
#include <asm/cpufeature.h>

#define RDRAND_RETRY_LOOPS	10

 

static inline bool __must_check rdrand_long(unsigned long *v)
{
	bool ok;
	unsigned int retry = RDRAND_RETRY_LOOPS;
	do {
		asm volatile("rdrand %[out]"
			     CC_SET(c)
			     : CC_OUT(c) (ok), [out] "=r" (*v));
		if (ok)
			return true;
	} while (--retry);
	return false;
}

static inline bool __must_check rdrand_int(unsigned int *v)
{
	bool ok;
	unsigned int retry = RDRAND_RETRY_LOOPS;
	do {
		asm volatile("rdrand %[out]"
			     CC_SET(c)
			     : CC_OUT(c) (ok), [out] "=r" (*v));
		if (ok)
			return true;
	} while (--retry);
	return false;
}

static inline bool __must_check rdseed_long(unsigned long *v)
{
	bool ok;
	asm volatile("rdseed %[out]"
		     CC_SET(c)
		     : CC_OUT(c) (ok), [out] "=r" (*v));
	return ok;
}

static inline bool __must_check rdseed_int(unsigned int *v)
{
	bool ok;
	asm volatile("rdseed %[out]"
		     CC_SET(c)
		     : CC_OUT(c) (ok), [out] "=r" (*v));
	return ok;
}

 

static inline void x86_init_rdrand(struct cpuinfo_x86 *c) { }


#endif  
