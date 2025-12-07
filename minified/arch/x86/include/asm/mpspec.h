
#ifndef _ASM_X86_MPSPEC_H
#define _ASM_X86_MPSPEC_H


#include <asm/x86_init.h>
#include <asm/apicdef.h>

/* Inlined from mpspec_def.h */
#define SMP_MAGIC_IDENT	(('_'<<24) | ('P'<<16) | ('M'<<8) | '_')
# define MAX_MPC_ENTRY 1024

struct mpf_intel {
	char signature[4];
	unsigned int physptr;
	unsigned char length;
	unsigned char specification;
	unsigned char checksum;
	unsigned char feature1;
	unsigned char feature2;
	unsigned char feature3;
	unsigned char feature4;
	unsigned char feature5;
};

#define MPC_SIGNATURE "PCMP"

struct mpc_table {
	char signature[4];
	unsigned short length;
	char spec;
	char checksum;
	char oem[8];
	char productid[12];
	unsigned int oemptr;
	unsigned short oemsize;
	unsigned short oemcount;
	unsigned int lapic;
	unsigned int reserved;
};

#define	MP_PROCESSOR		0
#define	MP_BUS			1
#define	MP_IOAPIC		2
#define	MP_INTSRC		3
#define	MP_LINTSRC		4
#define	MP_TRANSLATION		192

#define CPU_ENABLED		1
#define CPU_BOOTPROCESSOR	2

#define CPU_STEPPING_MASK	0x000F
#define CPU_MODEL_MASK		0x00F0
#define CPU_FAMILY_MASK		0x0F00

struct mpc_cpu {
	unsigned char type;
	unsigned char apicid;
	unsigned char apicver;
	unsigned char cpuflag;
	unsigned int cpufeature;
	unsigned int featureflag;
	unsigned int reserved[2];
};

struct mpc_bus {
	unsigned char type;
	unsigned char busid;
	unsigned char bustype[6];
};

#define MPC_APIC_USABLE		0x01

struct mpc_ioapic {
	unsigned char type;
	unsigned char apicid;
	unsigned char apicver;
	unsigned char flags;
	unsigned int apicaddr;
};

struct mpc_intsrc {
	unsigned char type;
	unsigned char irqtype;
	unsigned short irqflag;
	unsigned char srcbus;
	unsigned char srcbusirq;
	unsigned char dstapic;
	unsigned char dstirq;
};

enum mp_irq_source_types {
	mp_INT = 0,
	mp_NMI = 1,
	mp_SMI = 2,
	mp_ExtINT = 3
};

#define MP_IRQPOL_DEFAULT	0x0
#define MP_IRQPOL_ACTIVE_HIGH	0x1
#define MP_IRQPOL_RESERVED	0x2
#define MP_IRQPOL_ACTIVE_LOW	0x3
#define MP_IRQPOL_MASK		0x3

#define MP_IRQTRIG_DEFAULT	0x0
#define MP_IRQTRIG_EDGE		0x4
#define MP_IRQTRIG_RESERVED	0x8
#define MP_IRQTRIG_LEVEL	0xc
#define MP_IRQTRIG_MASK		0xc

#define MP_APIC_ALL	0xFF

struct mpc_lintsrc {
	unsigned char type;
	unsigned char irqtype;
	unsigned short irqflag;
	unsigned char srcbusid;
	unsigned char srcbusirq;
	unsigned char destapic;
	unsigned char destapiclint;
};

enum mp_bustype {
	MP_BUS_ISA = 1,
	MP_BUS_EISA,
	MP_BUS_PCI,
};
/* End of mpspec_def.h */

extern int pic_mode;


 
#if CONFIG_BASE_SMALL == 0
# define MAX_MP_BUSSES		260
#else
# define MAX_MP_BUSSES		32
#endif

#define MAX_IRQ_SOURCES		256

extern unsigned int def_to_bigsmp;



extern DECLARE_BITMAP(mp_bus_not_pci, MAX_MP_BUSSES);

extern unsigned int boot_cpu_physical_apicid;
extern u8 boot_cpu_apic_version;
extern unsigned long mp_lapic_addr;

# define smp_found_config 0

static inline void get_smp_config(void)
{
	x86_init.mpparse.get_smp_config(0);
}

static inline void early_get_smp_config(void)
{
	x86_init.mpparse.get_smp_config(1);
}

static inline void find_smp_config(void)
{
	x86_init.mpparse.find_smp_config();
}

static inline void e820__memblock_alloc_reserved_mpc_new(void) { }
#define enable_update_mptable 0
#define default_find_smp_config x86_init_noop
#define default_get_smp_config x86_init_uint_noop

int generic_processor_info(int apicid, int version);

#define PHYSID_ARRAY_SIZE	BITS_TO_LONGS(MAX_LOCAL_APIC)

struct physid_mask {
	unsigned long mask[PHYSID_ARRAY_SIZE];
};

typedef struct physid_mask physid_mask_t;

#define physid_set(physid, map)			set_bit(physid, (map).mask)
#define physid_clear(physid, map)		clear_bit(physid, (map).mask)
#define physid_isset(physid, map)		test_bit(physid, (map).mask)
#define physid_test_and_set(physid, map)			\
	test_and_set_bit(physid, (map).mask)

#define physids_and(dst, src1, src2)					\
	bitmap_and((dst).mask, (src1).mask, (src2).mask, MAX_LOCAL_APIC)

#define physids_or(dst, src1, src2)					\
	bitmap_or((dst).mask, (src1).mask, (src2).mask, MAX_LOCAL_APIC)

#define physids_clear(map)					\
	bitmap_zero((map).mask, MAX_LOCAL_APIC)

#define physids_complement(dst, src)				\
	bitmap_complement((dst).mask, (src).mask, MAX_LOCAL_APIC)

#define physids_empty(map)					\
	bitmap_empty((map).mask, MAX_LOCAL_APIC)

#define physids_equal(map1, map2)				\
	bitmap_equal((map1).mask, (map2).mask, MAX_LOCAL_APIC)

#define physids_weight(map)					\
	bitmap_weight((map).mask, MAX_LOCAL_APIC)

#define physids_shift_right(d, s, n)				\
	bitmap_shift_right((d).mask, (s).mask, n, MAX_LOCAL_APIC)

#define physids_shift_left(d, s, n)				\
	bitmap_shift_left((d).mask, (s).mask, n, MAX_LOCAL_APIC)

static inline unsigned long physids_coerce(physid_mask_t *map)
{
	return map->mask[0];
}

static inline void physids_promote(unsigned long physids, physid_mask_t *map)
{
	physids_clear(*map);
	map->mask[0] = physids;
}

static inline void physid_set_mask_of_physid(int physid, physid_mask_t *map)
{
	physids_clear(*map);
	physid_set(physid, *map);
}

#define PHYSID_MASK_ALL		{ {[0 ... PHYSID_ARRAY_SIZE-1] = ~0UL} }
#define PHYSID_MASK_NONE	{ {[0 ... PHYSID_ARRAY_SIZE-1] = 0UL} }

extern physid_mask_t phys_cpu_present_map;

#endif  
