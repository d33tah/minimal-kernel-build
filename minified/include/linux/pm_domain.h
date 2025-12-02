/* Minimal pm_domain.h - only stubs used by platform.c */
#ifndef _LINUX_PM_DOMAIN_H
#define _LINUX_PM_DOMAIN_H

#include <linux/device.h>

static inline int dev_pm_domain_attach(struct device *dev, bool power_on)
{
	return 0;
}
static inline void dev_pm_domain_detach(struct device *dev, bool power_off) {}
static inline int dev_pm_domain_start(struct device *dev)
{
	return 0;
}
static inline void dev_pm_domain_set(struct device *dev,
				     struct dev_pm_domain *pd) {}

#endif  
