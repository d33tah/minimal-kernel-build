#ifndef __X86_MEM_ENCRYPT_H__
#define __X86_MEM_ENCRYPT_H__

#ifndef __ASSEMBLY__

#include <linux/init.h>

#include <asm/bootparam.h>


#define sme_me_mask	0ULL

#define __sme_pa(x)		(__pa(x) | sme_me_mask)

#endif	

#endif
