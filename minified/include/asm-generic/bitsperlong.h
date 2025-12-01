#ifndef __ASM_GENERIC_BITS_PER_LONG
#define __ASM_GENERIC_BITS_PER_LONG




#define BITS_PER_LONG 32


#ifndef BITS_PER_LONG_LONG
#define BITS_PER_LONG_LONG 64
#endif

#define small_const_nbits(nbits) \
	(__builtin_constant_p(nbits) && (nbits) <= BITS_PER_LONG && (nbits) > 0)

#endif  
