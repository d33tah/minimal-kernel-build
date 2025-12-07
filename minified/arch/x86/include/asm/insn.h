
#ifndef _ASM_X86_INSN_H
#define _ASM_X86_INSN_H


#include <asm/byteorder.h>

/* Inlined from inat.h */
typedef unsigned int insn_attr_t;
typedef unsigned char insn_byte_t;
typedef signed int insn_value_t;

#define INAT_OPCODE_TABLE_SIZE 256
#define INAT_GROUP_TABLE_SIZE 8

#define INAT_PFX_OPNDSZ	1
#define INAT_PFX_REPE	2
#define INAT_PFX_REPNE	3
#define INAT_PFX_LOCK	4
#define INAT_PFX_CS	5
#define INAT_PFX_DS	6
#define INAT_PFX_ES	7
#define INAT_PFX_FS	8
#define INAT_PFX_GS	9
#define INAT_PFX_SS	10
#define INAT_PFX_ADDRSZ	11
#define INAT_PFX_REX	12
#define INAT_PFX_VEX2	13
#define INAT_PFX_VEX3	14
#define INAT_PFX_EVEX	15

#define INAT_LSTPFX_MAX	3
#define INAT_LGCPFX_MAX	11

#define INAT_IMM_BYTE		1
#define INAT_IMM_WORD		2
#define INAT_IMM_DWORD		3
#define INAT_IMM_QWORD		4
#define INAT_IMM_PTR		5
#define INAT_IMM_VWORD32	6
#define INAT_IMM_VWORD		7

#define INAT_PFX_OFFS	0
#define INAT_PFX_BITS	4
#define INAT_PFX_MAX    ((1 << INAT_PFX_BITS) - 1)
#define INAT_PFX_MASK	(INAT_PFX_MAX << INAT_PFX_OFFS)
#define INAT_ESC_OFFS	(INAT_PFX_OFFS + INAT_PFX_BITS)
#define INAT_ESC_BITS	2
#define INAT_ESC_MAX	((1 << INAT_ESC_BITS) - 1)
#define INAT_ESC_MASK	(INAT_ESC_MAX << INAT_ESC_OFFS)
#define INAT_GRP_OFFS	(INAT_ESC_OFFS + INAT_ESC_BITS)
#define INAT_GRP_BITS	5
#define INAT_GRP_MAX	((1 << INAT_GRP_BITS) - 1)
#define INAT_GRP_MASK	(INAT_GRP_MAX << INAT_GRP_OFFS)
#define INAT_IMM_OFFS	(INAT_GRP_OFFS + INAT_GRP_BITS)
#define INAT_IMM_BITS	3
#define INAT_IMM_MASK	(((1 << INAT_IMM_BITS) - 1) << INAT_IMM_OFFS)
#define INAT_FLAG_OFFS	(INAT_IMM_OFFS + INAT_IMM_BITS)
#define INAT_MODRM	(1 << (INAT_FLAG_OFFS))
#define INAT_FORCE64	(1 << (INAT_FLAG_OFFS + 1))
#define INAT_SCNDIMM	(1 << (INAT_FLAG_OFFS + 2))
#define INAT_MOFFSET	(1 << (INAT_FLAG_OFFS + 3))
#define INAT_VARIANT	(1 << (INAT_FLAG_OFFS + 4))
#define INAT_VEXOK	(1 << (INAT_FLAG_OFFS + 5))
#define INAT_VEXONLY	(1 << (INAT_FLAG_OFFS + 6))
#define INAT_EVEXONLY	(1 << (INAT_FLAG_OFFS + 7))
#define INAT_MAKE_PREFIX(pfx)	(pfx << INAT_PFX_OFFS)
#define INAT_MAKE_ESCAPE(esc)	(esc << INAT_ESC_OFFS)
#define INAT_MAKE_GROUP(grp)	((grp << INAT_GRP_OFFS) | INAT_MODRM)
#define INAT_MAKE_IMM(imm)	(imm << INAT_IMM_OFFS)

extern insn_attr_t inat_get_opcode_attribute(insn_byte_t opcode);
extern int inat_get_last_prefix_id(insn_byte_t last_pfx);
extern insn_attr_t inat_get_escape_attribute(insn_byte_t opcode, int lpfx_id, insn_attr_t esc_attr);
extern insn_attr_t inat_get_group_attribute(insn_byte_t modrm, int lpfx_id, insn_attr_t esc_attr);
extern insn_attr_t inat_get_avx_attribute(insn_byte_t opcode, insn_byte_t vex_m, insn_byte_t vex_pp);

static inline int inat_is_legacy_prefix(insn_attr_t attr) {
	attr &= INAT_PFX_MASK;
	return attr && attr <= INAT_LGCPFX_MAX;
}
static inline int inat_is_address_size_prefix(insn_attr_t attr) { return (attr & INAT_PFX_MASK) == INAT_PFX_ADDRSZ; }
static inline int inat_is_operand_size_prefix(insn_attr_t attr) { return (attr & INAT_PFX_MASK) == INAT_PFX_OPNDSZ; }
static inline int inat_is_rex_prefix(insn_attr_t attr) { return (attr & INAT_PFX_MASK) == INAT_PFX_REX; }
static inline int inat_last_prefix_id(insn_attr_t attr) {
	if ((attr & INAT_PFX_MASK) > INAT_LSTPFX_MAX) return 0;
	else return attr & INAT_PFX_MASK;
}
static inline int inat_is_vex_prefix(insn_attr_t attr) {
	attr &= INAT_PFX_MASK;
	return attr == INAT_PFX_VEX2 || attr == INAT_PFX_VEX3 || attr == INAT_PFX_EVEX;
}
static inline int inat_is_evex_prefix(insn_attr_t attr) { return (attr & INAT_PFX_MASK) == INAT_PFX_EVEX; }
static inline int inat_is_vex3_prefix(insn_attr_t attr) { return (attr & INAT_PFX_MASK) == INAT_PFX_VEX3; }
static inline int inat_is_escape(insn_attr_t attr) { return attr & INAT_ESC_MASK; }
static inline int inat_escape_id(insn_attr_t attr) { return (attr & INAT_ESC_MASK) >> INAT_ESC_OFFS; }
static inline int inat_is_group(insn_attr_t attr) { return attr & INAT_GRP_MASK; }
static inline int inat_group_id(insn_attr_t attr) { return (attr & INAT_GRP_MASK) >> INAT_GRP_OFFS; }
static inline int inat_group_common_attribute(insn_attr_t attr) { return attr & ~INAT_GRP_MASK; }
static inline int inat_has_immediate(insn_attr_t attr) { return attr & INAT_IMM_MASK; }
static inline int inat_immediate_size(insn_attr_t attr) { return (attr & INAT_IMM_MASK) >> INAT_IMM_OFFS; }
static inline int inat_has_modrm(insn_attr_t attr) { return attr & INAT_MODRM; }
static inline int inat_is_force64(insn_attr_t attr) { return attr & INAT_FORCE64; }
static inline int inat_has_second_immediate(insn_attr_t attr) { return attr & INAT_SCNDIMM; }
static inline int inat_has_moffset(insn_attr_t attr) { return attr & INAT_MOFFSET; }
static inline int inat_has_variant(insn_attr_t attr) { return attr & INAT_VARIANT; }
static inline int inat_accept_vex(insn_attr_t attr) { return attr & INAT_VEXOK; }
static inline int inat_must_vex(insn_attr_t attr) { return attr & (INAT_VEXONLY | INAT_EVEXONLY); }
static inline int inat_must_evex(insn_attr_t attr) { return attr & INAT_EVEXONLY; }
/* End of inat.h */

#if defined(__BYTE_ORDER) ? __BYTE_ORDER == __LITTLE_ENDIAN : defined(__LITTLE_ENDIAN)

struct insn_field {
	union {
		insn_value_t value;
		insn_byte_t bytes[4];
	};
	 
	unsigned char got;
	unsigned char nbytes;
};

static inline void insn_field_set(struct insn_field *p, insn_value_t v,
				  unsigned char n)
{
	p->value = v;
	p->nbytes = n;
}

static inline void insn_set_byte(struct insn_field *p, unsigned char n,
				 insn_byte_t v)
{
	p->bytes[n] = v;
}

#else

struct insn_field {
	insn_value_t value;
	union {
		insn_value_t little;
		insn_byte_t bytes[4];
	};
	 
	unsigned char got;
	unsigned char nbytes;
};

static inline void insn_field_set(struct insn_field *p, insn_value_t v,
				  unsigned char n)
{
	p->value = v;
	p->little = __cpu_to_le32(v);
	p->nbytes = n;
}

static inline void insn_set_byte(struct insn_field *p, unsigned char n,
				 insn_byte_t v)
{
	p->bytes[n] = v;
	p->value = __le32_to_cpu(p->little);
}
#endif

struct insn {
	struct insn_field prefixes;	 
	struct insn_field rex_prefix;	 
	struct insn_field vex_prefix;	 
	struct insn_field opcode;	 
	struct insn_field modrm;
	struct insn_field sib;
	struct insn_field displacement;
	union {
		struct insn_field immediate;
		struct insn_field moffset1;	 
		struct insn_field immediate1;	 
	};
	union {
		struct insn_field moffset2;	 
		struct insn_field immediate2;	 
	};

	int	emulate_prefix_size;
	insn_attr_t attr;
	unsigned char opnd_bytes;
	unsigned char addr_bytes;
	unsigned char length;
	unsigned char x86_64;

	const insn_byte_t *kaddr;	 
	const insn_byte_t *end_kaddr;	 
	const insn_byte_t *next_byte;
};

#define MAX_INSN_SIZE	15

#define X86_MODRM_MOD(modrm) (((modrm) & 0xc0) >> 6)
#define X86_MODRM_REG(modrm) (((modrm) & 0x38) >> 3)
#define X86_MODRM_RM(modrm) ((modrm) & 0x07)

#define X86_SIB_SCALE(sib) (((sib) & 0xc0) >> 6)
#define X86_SIB_INDEX(sib) (((sib) & 0x38) >> 3)
#define X86_SIB_BASE(sib) ((sib) & 0x07)

#define X86_REX_W(rex) ((rex) & 8)
#define X86_REX_R(rex) ((rex) & 4)
#define X86_REX_X(rex) ((rex) & 2)
#define X86_REX_B(rex) ((rex) & 1)

 
#define X86_VEX_W(vex)	((vex) & 0x80)	 
#define X86_VEX_R(vex)	((vex) & 0x80)	 
#define X86_VEX_X(vex)	((vex) & 0x40)	 
#define X86_VEX_B(vex)	((vex) & 0x20)	 
#define X86_VEX_L(vex)	((vex) & 0x04)	 
 
#define X86_EVEX_M(vex)	((vex) & 0x07)		 
#define X86_VEX3_M(vex)	((vex) & 0x1f)		 
#define X86_VEX2_M	1			 
#define X86_VEX_V(vex)	(((vex) & 0x78) >> 3)	 
#define X86_VEX_P(vex)	((vex) & 0x03)		 
#define X86_VEX_M_MAX	0x1f			 

extern void insn_init(struct insn *insn, const void *kaddr, int buf_len, int x86_64);
extern int insn_get_prefixes(struct insn *insn);
extern int insn_get_opcode(struct insn *insn);
extern int insn_get_modrm(struct insn *insn);
extern int insn_get_sib(struct insn *insn);
extern int insn_get_displacement(struct insn *insn);
extern int insn_get_immediate(struct insn *insn);
extern int insn_get_length(struct insn *insn);

enum insn_mode {
	INSN_MODE_32,
	INSN_MODE_64,
	 
	INSN_MODE_KERN,
	INSN_NUM_MODES,
};

extern int insn_decode(struct insn *insn, const void *kaddr, int buf_len, enum insn_mode m);

#define insn_decode_kernel(_insn, _ptr) insn_decode((_insn), (_ptr), MAX_INSN_SIZE, INSN_MODE_KERN)

 
static inline void insn_get_attribute(struct insn *insn)
{
	insn_get_modrm(insn);
}

 
extern int insn_rip_relative(struct insn *insn);

static inline int insn_is_avx(struct insn *insn)
{
	if (!insn->prefixes.got)
		insn_get_prefixes(insn);
	return (insn->vex_prefix.value != 0);
}

static inline int insn_is_evex(struct insn *insn)
{
	if (!insn->prefixes.got)
		insn_get_prefixes(insn);
	return (insn->vex_prefix.nbytes == 4);
}

static inline int insn_has_emulate_prefix(struct insn *insn)
{
	return !!insn->emulate_prefix_size;
}

static inline insn_byte_t insn_vex_m_bits(struct insn *insn)
{
	if (insn->vex_prefix.nbytes == 2)	 
		return X86_VEX2_M;
	else if (insn->vex_prefix.nbytes == 3)	 
		return X86_VEX3_M(insn->vex_prefix.bytes[1]);
	else					 
		return X86_EVEX_M(insn->vex_prefix.bytes[1]);
}

static inline insn_byte_t insn_vex_p_bits(struct insn *insn)
{
	if (insn->vex_prefix.nbytes == 2)	 
		return X86_VEX_P(insn->vex_prefix.bytes[1]);
	else
		return X86_VEX_P(insn->vex_prefix.bytes[2]);
}

 
static inline int insn_last_prefix_id(struct insn *insn)
{
	if (insn_is_avx(insn))
		return insn_vex_p_bits(insn);	 

	if (insn->prefixes.bytes[3])
		return inat_get_last_prefix_id(insn->prefixes.bytes[3]);

	return 0;
}

 
static inline int insn_offset_rex_prefix(struct insn *insn)
{
	return insn->prefixes.nbytes;
}
static inline int insn_offset_vex_prefix(struct insn *insn)
{
	return insn_offset_rex_prefix(insn) + insn->rex_prefix.nbytes;
}
static inline int insn_offset_opcode(struct insn *insn)
{
	return insn_offset_vex_prefix(insn) + insn->vex_prefix.nbytes;
}
static inline int insn_offset_modrm(struct insn *insn)
{
	return insn_offset_opcode(insn) + insn->opcode.nbytes;
}
static inline int insn_offset_sib(struct insn *insn)
{
	return insn_offset_modrm(insn) + insn->modrm.nbytes;
}
static inline int insn_offset_displacement(struct insn *insn)
{
	return insn_offset_sib(insn) + insn->sib.nbytes;
}
static inline int insn_offset_immediate(struct insn *insn)
{
	return insn_offset_displacement(insn) + insn->displacement.nbytes;
}

 
#define for_each_insn_prefix(insn, idx, prefix)	\
	for (idx = 0; idx < ARRAY_SIZE(insn->prefixes.bytes) && (prefix = insn->prefixes.bytes[idx]) != 0; idx++)

#define POP_SS_OPCODE 0x1f
#define MOV_SREG_OPCODE 0x8e

 
static inline int insn_masking_exception(struct insn *insn)
{
	return insn->opcode.bytes[0] == POP_SS_OPCODE ||
		(insn->opcode.bytes[0] == MOV_SREG_OPCODE &&
		 X86_MODRM_REG(insn->modrm.bytes[0]) == 2);
}

#endif  
