 
 

#ifndef __X86_MEM_ENCRYPT_H__
#define __X86_MEM_ENCRYPT_H__

#ifndef __ASSEMBLY__

#include <linux/init.h>
#include <linux/cc_platform.h>

#include <asm/bootparam.h>


#define sme_me_mask	0ULL

/* Only keeping functions that are actually called */
static inline void __init sev_setup_arch(void) { }
static inline void sev_es_init_vc_handling(void) { }
static inline void mem_encrypt_free_decrypted_mem(void) { }
/* Removed unused: sme_early_encrypt, sme_early_decrypt, sme_map_bootdata,
   sme_unmap_bootdata, sme_early_init, sme_encrypt_kernel, sme_enable,
   early_set_memory_decrypted, early_set_memory_encrypted,
   early_set_mem_enc_dec_hypercall */

#define __bss_decrypted


 
void __init mem_encrypt_init(void);

 
#define __sme_pa(x)		(__pa(x) | sme_me_mask)
#define __sme_pa_nodebug(x)	(__pa_nodebug(x) | sme_me_mask)

extern char __start_bss_decrypted[], __end_bss_decrypted[], __start_bss_decrypted_unused[];

static inline u64 sme_get_me_mask(void)
{
	return sme_me_mask;
}

#endif	 

#endif	 
