 
#ifndef _ASM_X86_MICROCODE_INTEL_H
#define _ASM_X86_MICROCODE_INTEL_H

#include <asm/microcode.h>

struct microcode_header_intel {
	unsigned int            hdrver;
	unsigned int            rev;
	unsigned int            date;
	unsigned int            sig;
	unsigned int            cksum;
	unsigned int            ldrver;
	unsigned int            pf;
	unsigned int            datasize;
	unsigned int            totalsize;
	unsigned int            reserved[3];
};

struct microcode_intel {
	struct microcode_header_intel hdr;
	unsigned int            bits[];
};

 
struct extended_signature {
	unsigned int            sig;
	unsigned int            pf;
	unsigned int            cksum;
};

struct extended_sigtable {
	unsigned int            count;
	unsigned int            cksum;
	unsigned int            reserved[3];
	struct extended_signature sigs[];
};

#define DEFAULT_UCODE_DATASIZE	(2000)
#define MC_HEADER_SIZE		(sizeof(struct microcode_header_intel))
#define DEFAULT_UCODE_TOTALSIZE (DEFAULT_UCODE_DATASIZE + MC_HEADER_SIZE)
#define EXT_HEADER_SIZE		(sizeof(struct extended_sigtable))
#define EXT_SIGNATURE_SIZE	(sizeof(struct extended_signature))

#define get_totalsize(mc) \
	(((struct microcode_intel *)mc)->hdr.datasize ? \
	 ((struct microcode_intel *)mc)->hdr.totalsize : \
	 DEFAULT_UCODE_TOTALSIZE)

#define get_datasize(mc) \
	(((struct microcode_intel *)mc)->hdr.datasize ? \
	 ((struct microcode_intel *)mc)->hdr.datasize : DEFAULT_UCODE_DATASIZE)

#define exttable_size(et) ((et)->count * EXT_SIGNATURE_SIZE + EXT_HEADER_SIZE)

/* intel_get_microcode_revision, load_ucode_intel_bsp, load_ucode_intel_ap,
   save_microcode_in_initrd_intel, reload_ucode_intel removed - unused */
static inline void show_ucode_info_early(void) {}

#endif
