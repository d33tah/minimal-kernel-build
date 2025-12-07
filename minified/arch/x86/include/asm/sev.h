


#ifndef __ASM_ENCRYPTED_STATE_H
#define __ASM_ENCRYPTED_STATE_H

#include <linux/types.h>
#include <asm/insn.h>
#include <asm/bootparam.h>

/* Inlined from sev-common.h */
#define GHCB_MSR_INFO_POS		0
#define GHCB_DATA_LOW			12
#define GHCB_MSR_INFO_MASK		(BIT_ULL(GHCB_DATA_LOW) - 1)

#define GHCB_DATA(v)			\
	(((unsigned long)(v) & ~GHCB_MSR_INFO_MASK) >> GHCB_DATA_LOW)

#define GHCB_MSR_SEV_INFO_RESP		0x001
#define GHCB_MSR_SEV_INFO_REQ		0x002

#define GHCB_MSR_SEV_INFO(_max, _min, _cbit)	\
	((((_max) & 0xffff) << 48) |		\
	 (((_min) & 0xffff) << 32) |		\
	 (((_cbit) & 0xff)  << 24) |		\
	 GHCB_MSR_SEV_INFO_RESP)

#define GHCB_MSR_INFO(v)		((v) & 0xfffUL)
#define GHCB_MSR_PROTO_MAX(v)		(((v) >> 48) & 0xffff)
#define GHCB_MSR_PROTO_MIN(v)		(((v) >> 32) & 0xffff)

#define GHCB_MSR_CPUID_REQ		0x004
#define GHCB_MSR_CPUID_RESP		0x005
#define GHCB_MSR_CPUID_FUNC_POS		32
#define GHCB_MSR_CPUID_FUNC_MASK	0xffffffff
#define GHCB_MSR_CPUID_VALUE_POS	32
#define GHCB_MSR_CPUID_VALUE_MASK	0xffffffff
#define GHCB_MSR_CPUID_REG_POS		30
#define GHCB_MSR_CPUID_REG_MASK		0x3
#define GHCB_CPUID_REQ_EAX		0
#define GHCB_CPUID_REQ_EBX		1
#define GHCB_CPUID_REQ_ECX		2
#define GHCB_CPUID_REQ_EDX		3
#define GHCB_CPUID_REQ(fn, reg)				\
	(GHCB_MSR_CPUID_REQ |				\
	(((unsigned long)(reg) & 0x3) << 30) |		\
	(((unsigned long)fn) << 32))

#define GHCB_MSR_AP_RESET_HOLD_REQ	0x006
#define GHCB_MSR_AP_RESET_HOLD_RESP	0x007

#define GHCB_MSR_REG_GPA_REQ		0x012
#define GHCB_MSR_REG_GPA_REQ_VAL(v)			\
	(((u64)((v) & GENMASK_ULL(51, 0)) << 12) |	\
	GHCB_MSR_REG_GPA_REQ)

#define GHCB_MSR_REG_GPA_RESP		0x013
#define GHCB_MSR_REG_GPA_RESP_VAL(v)			\
	(((u64)(v) & GENMASK_ULL(63, 12)) >> 12)

enum psc_op {
	SNP_PAGE_STATE_PRIVATE = 1,
	SNP_PAGE_STATE_SHARED,
};

#define GHCB_MSR_PSC_REQ		0x014
#define GHCB_MSR_PSC_REQ_GFN(gfn, op)			\
	(((u64)((op) & 0xf) << 52) |			\
	((u64)((gfn) & GENMASK_ULL(39, 0)) << 12) |	\
	GHCB_MSR_PSC_REQ)

#define GHCB_MSR_PSC_RESP		0x015
#define GHCB_MSR_PSC_RESP_VAL(val)			\
	(((u64)(val) & GENMASK_ULL(63, 32)) >> 32)

#define GHCB_MSR_HV_FT_REQ		0x080
#define GHCB_MSR_HV_FT_RESP		0x081
#define GHCB_MSR_HV_FT_RESP_VAL(v)			\
	(((u64)(v) & GENMASK_ULL(63, 12)) >> 12)

#define GHCB_HV_FT_SNP			BIT_ULL(0)
#define GHCB_HV_FT_SNP_AP_CREATION	BIT_ULL(1)

#define VMGEXIT_PSC_MAX_ENTRY		253

struct psc_hdr {
	u16 cur_entry;
	u16 end_entry;
	u32 reserved;
} __packed;

struct psc_entry {
	u64	cur_page	: 12,
		gfn		: 40,
		operation	: 4,
		pagesize	: 1,
		reserved	: 7;
} __packed;

struct snp_psc_desc {
	struct psc_hdr hdr;
	struct psc_entry entries[VMGEXIT_PSC_MAX_ENTRY];
} __packed;

#define SNP_GUEST_REQ_INVALID_LEN	BIT_ULL(32)

#define GHCB_MSR_TERM_REQ		0x100
#define GHCB_MSR_TERM_REASON_SET_POS	12
#define GHCB_MSR_TERM_REASON_SET_MASK	0xf
#define GHCB_MSR_TERM_REASON_POS	16
#define GHCB_MSR_TERM_REASON_MASK	0xff

#define GHCB_SEV_TERM_REASON(reason_set, reason_val)	\
	(((((u64)reason_set) &  0xf) << 12) |		\
	((((u64)reason_val) & 0xff) << 16))

#define SEV_TERM_SET_GEN		0
#define GHCB_SEV_ES_GEN_REQ		0
#define GHCB_SEV_ES_PROT_UNSUPPORTED	1
#define GHCB_SNP_UNSUPPORTED		2

#define SEV_TERM_SET_LINUX		1
#define GHCB_TERM_REGISTER		0
#define GHCB_TERM_PSC			1
#define GHCB_TERM_PVALIDATE		2
#define GHCB_TERM_NOT_VMPL0		3
#define GHCB_TERM_CPUID			4
#define GHCB_TERM_CPUID_HV		5

#define GHCB_RESP_CODE(v)		((v) & GHCB_MSR_INFO_MASK)

#define GHCB_ERR_NOT_REGISTERED		1
#define GHCB_ERR_INVALID_USAGE		2
#define GHCB_ERR_INVALID_SCRATCH_AREA	3
#define GHCB_ERR_MISSING_INPUT		4
#define GHCB_ERR_INVALID_INPUT		5
#define GHCB_ERR_INVALID_EVENT		6
/* End of sev-common.h */

#define GHCB_PROTOCOL_MIN	1ULL
#define GHCB_PROTOCOL_MAX	2ULL
#define GHCB_DEFAULT_USAGE	0ULL

#define	VMGEXIT()			{ asm volatile("rep; vmmcall\n\r"); }

enum es_result {
	ES_OK,			 
	ES_UNSUPPORTED,		 
	ES_VMM_ERROR,		 
	ES_DECODE_FAILED,	 
	ES_EXCEPTION,		 
	ES_RETRY,		 
};

struct es_fault_info {
	unsigned long vector;
	unsigned long error_code;
	unsigned long cr2;
};

struct pt_regs;

 
struct es_em_ctxt {
	struct pt_regs *regs;
	struct insn insn;
	struct es_fault_info fi;
};

 
#define CC_BLOB_SEV_HDR_MAGIC	0x45444d41
struct cc_blob_sev_info {
	u32 magic;
	u16 version;
	u16 reserved;
	u64 secrets_phys;
	u32 secrets_len;
	u32 rsvd1;
	u64 cpuid_phys;
	u32 cpuid_len;
	u32 rsvd2;
} __packed;

void do_vc_no_ghcb(struct pt_regs *regs, unsigned long exit_code);

static inline u64 lower_bits(u64 val, unsigned int bits)
{
	u64 mask = (1ULL << bits) - 1;

	return (val & mask);
}

struct real_mode_header;
enum stack_type;
struct ghcb;

 
extern void vc_no_ghcb(void);
extern void vc_boot_ghcb(void);
extern bool handle_vc_boot_ghcb(struct pt_regs *regs);

 
#define PVALIDATE_FAIL_NOUPDATE		255

 
#define RMP_PG_SIZE_4K			0

#define RMPADJUST_VMSA_PAGE_BIT		BIT(16)

 
struct snp_req_data {
	unsigned long req_gpa;
	unsigned long resp_gpa;
	unsigned long data_gpa;
	unsigned int data_npages;
};

struct sev_guest_platform_data {
	u64 secrets_gpa;
};

 
struct secrets_os_area {
	u32 msg_seqno_0;
	u32 msg_seqno_1;
	u32 msg_seqno_2;
	u32 msg_seqno_3;
	u64 ap_jump_table_pa;
	u8 rsvd[40];
	u8 guest_usage[32];
} __packed;

#define VMPCK_KEY_LEN		32

 
struct snp_secrets_page_layout {
	u32 version;
	u32 imien	: 1,
	    rsvd1	: 31;
	u32 fms;
	u32 rsvd2;
	u8 gosvw[16];
	u8 vmpck0[VMPCK_KEY_LEN];
	u8 vmpck1[VMPCK_KEY_LEN];
	u8 vmpck2[VMPCK_KEY_LEN];
	u8 vmpck3[VMPCK_KEY_LEN];
	struct secrets_os_area os_area;
	u8 rsvd3[3840];
} __packed;

static inline void sev_es_ist_enter(struct pt_regs *regs) { }
static inline void sev_es_ist_exit(void) { }
static inline int sev_es_setup_ap_jump_table(struct real_mode_header *rmh) { return 0; }
static inline void sev_es_nmi_complete(void) { }
static inline int sev_es_efi_map_ghcbs(pgd_t *pgd) { return 0; }
static inline int pvalidate(unsigned long vaddr, bool rmp_psize, bool validate) { return 0; }
static inline int rmpadjust(unsigned long vaddr, bool rmp_psize, unsigned long attrs) { return 0; }
static inline void setup_ghcb(void) { }
static inline void __init
early_snp_set_memory_private(unsigned long vaddr, unsigned long paddr, unsigned int npages) { }
static inline void __init
early_snp_set_memory_shared(unsigned long vaddr, unsigned long paddr, unsigned int npages) { }
static inline void __init snp_prep_memory(unsigned long paddr, unsigned int sz, enum psc_op op) { }
static inline void snp_set_memory_shared(unsigned long vaddr, unsigned int npages) { }
static inline void snp_set_memory_private(unsigned long vaddr, unsigned int npages) { }
static inline void snp_set_wakeup_secondary_cpu(void) { }
static inline bool snp_init(struct boot_params *bp) { return false; }
static inline void snp_abort(void) { }
static inline int snp_issue_guest_request(u64 exit_code, struct snp_req_data *input,
					  unsigned long *fw_err)
{
	return -ENOTTY;
}

#endif
