/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _XEN_XEN_H
#define _XEN_XEN_H

enum xen_domain_type {
	XEN_NATIVE,		/* running on bare hardware    */
	XEN_PV_DOMAIN,		/* running in a PV domain      */
	XEN_HVM_DOMAIN,		/* running in a Xen hvm domain */
};

#define xen_domain_type		XEN_NATIVE

#define xen_pvh			0

#define xen_domain()		(xen_domain_type != XEN_NATIVE)
#define xen_pv_domain()		(xen_domain_type == XEN_PV_DOMAIN)
#define xen_hvm_domain()	(xen_domain_type == XEN_HVM_DOMAIN)
#define xen_pvh_domain()	(xen_pvh)

#include <linux/types.h>

extern uint32_t xen_start_flags;

#include <xen/interface/hvm/start_info.h>
extern struct hvm_start_info pvh_start_info;

#define xen_initial_domain()	(0)

struct bio_vec;
struct page;

bool xen_biovec_phys_mergeable(const struct bio_vec *vec1,
		const struct page *page);


#include <linux/platform-feature.h>

static inline void xen_set_restricted_virtio_memory_access(void)
{
	if (IS_ENABLED(CONFIG_XEN_VIRTIO) && xen_domain())
		platform_set(PLATFORM_VIRTIO_RESTRICTED_MEM_ACCESS);
}

#include <xen/balloon.h>
static inline int xen_alloc_unpopulated_pages(unsigned int nr_pages,
		struct page **pages)
{
	return xen_alloc_ballooned_pages(nr_pages, pages);
}
static inline void xen_free_unpopulated_pages(unsigned int nr_pages,
		struct page **pages)
{
	xen_free_ballooned_pages(nr_pages, pages);
}

#endif	/* _XEN_XEN_H */
