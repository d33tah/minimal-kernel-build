 
 

#ifndef __DMAR_H__
#define __DMAR_H__

#include <linux/acpi.h>
#include <linux/types.h>
#include <linux/msi.h>
#include <linux/irqreturn.h>
#include <linux/rwsem.h>
#include <linux/rculist.h>

struct acpi_dmar_header;

# define	DMAR_UNITS_SUPPORTED	MAX_IO_APICS

 
#define DMAR_INTR_REMAP		0x1
#define DMAR_X2APIC_OPT_OUT	0x2
#define DMAR_PLATFORM_OPT_IN	0x4

struct intel_iommu;

struct dmar_dev_scope {
	struct device __rcu *dev;
	u8 bus;
	u8 devfn;
};


static inline int dmar_device_add(void *handle)
{
	return 0;
}

static inline int dmar_device_remove(void *handle)
{
	return 0;
}

static inline bool dmar_platform_optin(void)
{
	return false;
}

static inline void detect_intel_iommu(void)
{
}


struct irte {
	union {
		 
		struct {
			__u64	present		: 1,   
				fpd		: 1,   
				__res0		: 6,   
				avail		: 4,   
				__res1		: 3,   
				pst		: 1,   
				vector		: 8,   
				__res2		: 40;  
		};

		 
		struct {
			__u64	r_present	: 1,   
				r_fpd		: 1,   
				dst_mode	: 1,   
				redir_hint	: 1,   
				trigger_mode	: 1,   
				dlvry_mode	: 3,   
				r_avail		: 4,   
				r_res0		: 4,   
				r_vector	: 8,   
				r_res1		: 8,   
				dest_id		: 32;  
		};

		 
		struct {
			__u64	p_present	: 1,   
				p_fpd		: 1,   
				p_res0		: 6,   
				p_avail		: 4,   
				p_res1		: 2,   
				p_urgent	: 1,   
				p_pst		: 1,   
				p_vector	: 8,   
				p_res2		: 14,  
				pda_l		: 26;  
		};
		__u64 low;
	};

	union {
		 
		struct {
			__u64	sid		: 16,   
				sq		: 2,    
				svt		: 2,    
				__res3		: 44;   
		};

		 
		struct {
			__u64	p_sid		: 16,   
				p_sq		: 2,    
				p_svt		: 2,    
				p_res3		: 12,   
				pda_h		: 32;   
		};
		__u64 high;
	};
};

static inline void dmar_copy_shared_irte(struct irte *dst, struct irte *src)
{
	dst->present	= src->present;
	dst->fpd	= src->fpd;
	dst->avail	= src->avail;
	dst->pst	= src->pst;
	dst->vector	= src->vector;
	dst->sid	= src->sid;
	dst->sq		= src->sq;
	dst->svt	= src->svt;
}

#define PDA_LOW_BIT    26
#define PDA_HIGH_BIT   32

 
struct irq_data;
extern void dmar_msi_unmask(struct irq_data *data);
extern void dmar_msi_mask(struct irq_data *data);
extern void dmar_msi_read(int irq, struct msi_msg *msg);
extern void dmar_msi_write(int irq, struct msi_msg *msg);
extern int dmar_set_interrupt(struct intel_iommu *iommu);
extern irqreturn_t dmar_fault(int irq, void *dev_id);
extern int dmar_alloc_hwirq(int id, int node, void *arg);
extern void dmar_free_hwirq(int irq);

#endif  
