
#ifndef _DEVICE_H_
#define _DEVICE_H_

/* --- 2025-12-07 23:58 --- Inlined from linux/dev_printk.h */
#include <linux/compiler.h>

#ifndef dev_fmt
#define dev_fmt(fmt) fmt
#endif

#define PRINTK_INFO_SUBSYSTEM_LEN	16
#define PRINTK_INFO_DEVICE_LEN		48

struct dev_printk_info {
	char subsystem[PRINTK_INFO_SUBSYSTEM_LEN];
	char device[PRINTK_INFO_DEVICE_LEN];
};

#define dev_printk(level, dev, fmt, ...) do { } while (0)
#define dev_emerg(dev, fmt, ...) do { } while (0)
#define dev_crit(dev, fmt, ...) do { } while (0)
#define dev_alert(dev, fmt, ...) do { } while (0)
#define dev_err(dev, fmt, ...) do { } while (0)
#define dev_warn(dev, fmt, ...) do { } while (0)
#define dev_notice(dev, fmt, ...) do { } while (0)
#define dev_info(dev, fmt, ...) do { } while (0)
#define dev_dbg(dev, fmt, ...) do { } while (0)

#define dev_emerg_once(dev, fmt, ...) do { } while (0)
#define dev_alert_once(dev, fmt, ...) do { } while (0)
#define dev_crit_once(dev, fmt, ...) do { } while (0)
#define dev_err_once(dev, fmt, ...) do { } while (0)
#define dev_warn_once(dev, fmt, ...) do { } while (0)
#define dev_notice_once(dev, fmt, ...) do { } while (0)
#define dev_info_once(dev, fmt, ...) do { } while (0)
#define dev_dbg_once(dev, fmt, ...) do { } while (0)

#define dev_emerg_ratelimited(dev, fmt, ...) do { } while (0)
#define dev_alert_ratelimited(dev, fmt, ...) do { } while (0)
#define dev_crit_ratelimited(dev, fmt, ...) do { } while (0)
#define dev_err_ratelimited(dev, fmt, ...) do { } while (0)
#define dev_warn_ratelimited(dev, fmt, ...) do { } while (0)
#define dev_notice_ratelimited(dev, fmt, ...) do { } while (0)
#define dev_info_ratelimited(dev, fmt, ...) do { } while (0)
#define dev_dbg_ratelimited(dev, fmt, ...) do { } while (0)

#define dev_vdbg(dev, fmt, ...) do { } while (0)
#define dev_WARN(dev, format, arg...) do { } while (0)
#define dev_WARN_ONCE(dev, condition, format, arg...) (0)
/* end dev_printk.h */

#include <linux/ioport.h>
#include <linux/kobject.h>
#include <linux/klist.h>
#include <linux/list.h>
#include <linux/lockdep.h>
#include <linux/compiler.h>
#include <linux/types.h>
#include <linux/mutex.h>
#include <linux/pm.h>
#include <linux/atomic.h>
#include <linux/uidgid.h>
#include <linux/gfp.h>
#include <linux/overflow.h>
#include <linux/device/bus.h>
#include <linux/device/class.h>
#include <linux/device/driver.h>
/* asm/device.h inlined */
struct dev_archdata { };
struct pdev_archdata { };

struct device;
struct device_private;
struct device_driver;
struct driver_private;
struct module;
struct class;
struct subsys_private;
struct device_node;
struct fwnode_handle;
struct iommu_ops;
struct iommu_group;
struct dev_pin_info;
struct dev_iommu;
struct msi_device_data;

struct subsys_interface {
	const char *name;
	struct bus_type *subsys;
	struct list_head node;
	int (*add_dev)(struct device *dev, struct subsys_interface *sif);
	void (*remove_dev)(struct device *dev, struct subsys_interface *sif);
};


int subsys_system_register(struct bus_type *subsys,
			   const struct attribute_group **groups);

struct device_type {
	const char *name;
	const struct attribute_group **groups;
	int (*uevent)(struct device *dev, struct kobj_uevent_env *env);
	char *(*devnode)(struct device *dev, umode_t *mode,
			 kuid_t *uid, kgid_t *gid);
	void (*release)(struct device *dev);

	const struct dev_pm_ops *pm;
};

struct device_attribute {
	struct attribute	attr;
	ssize_t (*show)(struct device *dev, struct device_attribute *attr,
			char *buf);
	ssize_t (*store)(struct device *dev, struct device_attribute *attr,
			 const char *buf, size_t count);
};

#define DEVICE_ATTR(_name, _mode, _show, _store) \
	struct device_attribute dev_attr_##_name = __ATTR(_name, _mode, _show, _store)
#define DEVICE_ATTR_RW(_name) \
	struct device_attribute dev_attr_##_name = __ATTR_RW(_name)
#define DEVICE_ATTR_RO(_name) \
	struct device_attribute dev_attr_##_name = __ATTR_RO(_name)
#define DEVICE_ATTR_WO(_name) \
	struct device_attribute dev_attr_##_name = __ATTR_WO(_name)

int device_create_file(struct device *device,
		       const struct device_attribute *entry);
void device_remove_file(struct device *dev,
			const struct device_attribute *attr);

typedef void (*dr_release_t)(struct device *dev, void *res);
typedef int (*dr_match_t)(struct device *dev, void *res, void *match_data);

void *__devres_alloc_node(dr_release_t release, size_t size, gfp_t gfp,
			  int nid, const char *name) __malloc;
#define devres_alloc(release, size, gfp) \
	__devres_alloc_node(release, size, gfp, NUMA_NO_NODE, #release)
#define devres_alloc_node(release, size, gfp, nid) \
	__devres_alloc_node(release, size, gfp, nid, #release)

void devres_for_each_res(struct device *dev, dr_release_t release,
			 dr_match_t match, void *match_data,
			 void (*fn)(struct device *, void *, void *),
			 void *data);
void devres_free(void *res);
void devres_add(struct device *dev, void *res);
void *devres_find(struct device *dev, dr_release_t release,
		  dr_match_t match, void *match_data);
void *devres_get(struct device *dev, void *new_res,
		 dr_match_t match, void *match_data);
void *devres_remove(struct device *dev, dr_release_t release,
		    dr_match_t match, void *match_data);
int devres_destroy(struct device *dev, dr_release_t release,
		   dr_match_t match, void *match_data);
int devres_release(struct device *dev, dr_release_t release,
		   dr_match_t match, void *match_data);


__printf(3, 4) char *devm_kasprintf(struct device *dev, gfp_t gfp,
				    const char *fmt, ...) __malloc;
char *devm_kstrdup(struct device *dev, const char *s, gfp_t gfp) __malloc;

/* devm_kmalloc, devm_krealloc, devm_kvasprintf, devm_kfree,
   devm_kstrdup_const, devm_kmemdup, devm_get_free_pages,
   devm_free_pages removed - none are called in minimal kernel */

void __iomem *devm_ioremap_resource(struct device *dev,
				    const struct resource *res);

struct device_dma_parameters {
	 
	unsigned int max_segment_size;
	unsigned int min_align_mask;
	unsigned long segment_boundary_mask;
};

enum device_link_state {
	DL_STATE_NONE = -1,
};

#define DL_FLAG_STATELESS		BIT(0)
#define DL_FLAG_AUTOREMOVE_CONSUMER	BIT(1)
#define DL_FLAG_PM_RUNTIME		BIT(2)
#define DL_FLAG_RPM_ACTIVE		BIT(3)
#define DL_FLAG_AUTOREMOVE_SUPPLIER	BIT(4)
#define DL_FLAG_AUTOPROBE_CONSUMER	BIT(5)
#define DL_FLAG_MANAGED			BIT(6)
#define DL_FLAG_SYNC_STATE_ONLY		BIT(7)
#define DL_FLAG_INFERRED		BIT(8)

enum dl_dev_state {
	DL_DEV_NO_DRIVER = 0,
	DL_DEV_PROBING,
};

enum device_removable {
	DEVICE_REMOVABLE_NOT_SUPPORTED = 0,
};

struct dev_links_info {
	struct list_head suppliers;
	struct list_head consumers;
	struct list_head defer_sync;
	enum dl_dev_state status;
};

struct dev_msi_info {
};

/* device_physical_location - not used in minimal kernel */
struct device_physical_location;

struct device {
	struct kobject kobj;
	struct device		*parent;

	struct device_private	*p;

	const char		*init_name;  
	const struct device_type *type;

	struct bus_type	*bus;		 
	struct device_driver *driver;	 
	void		*platform_data;	 
	void		*driver_data;	 
	struct mutex		mutex;	 

	struct dev_links_info	links;
	struct dev_pm_info	power;
	struct dev_pm_domain	*pm_domain;


	struct dev_msi_info	msi;
	u64		*dma_mask;	 
	u64		coherent_dma_mask; 
	u64		bus_dma_limit;	 
	const struct bus_dma_region *dma_range_map;

	struct device_dma_parameters *dma_parms;

	struct list_head	dma_pools;	 

	 
	struct dev_archdata	archdata;

	struct device_node	*of_node;  
	struct fwnode_handle	*fwnode;  

	dev_t			devt;	 
	u32			id;	 

	spinlock_t		devres_lock;
	struct list_head	devres_head;

	struct class		*class;
	const struct attribute_group **groups;	 

	void	(*release)(struct device *dev);
	struct iommu_group	*iommu_group;
	struct dev_iommu	*iommu;

	struct device_physical_location *physical_location;

	enum device_removable	removable;

	bool			offline_disabled:1;
	bool			offline:1;
	bool			of_node_reused:1;
	bool			state_synced:1;
	bool			can_match:1;
#if defined(CONFIG_ARCH_HAS_SYNC_DMA_FOR_DEVICE) || \
    defined(CONFIG_ARCH_HAS_SYNC_DMA_FOR_CPU) || \
    defined(CONFIG_ARCH_HAS_SYNC_DMA_FOR_CPU_ALL)
	bool			dma_coherent:1;
#endif
};

struct device_link {
	struct device *supplier;
	struct list_head s_node;
	struct device *consumer;
	struct list_head c_node;
	struct device link_dev;
	enum device_link_state status;
	u32 flags;
	refcount_t rpm_active;
	struct kref kref;
	struct work_struct rm_work;
	bool supplier_preactivated;  
};

static inline struct device *kobj_to_dev(struct kobject *kobj)
{
	return container_of(kobj, struct device, kobj);
}



/* pm_wakeup.h content removed - wakeup functions not used in minimal kernel */

static inline const char *dev_name(const struct device *dev)
{
	 
	if (dev->init_name)
		return dev->init_name;

	return kobject_name(&dev->kobj);
}

static inline const char *dev_bus_name(const struct device *dev)
{
	return dev->bus ? dev->bus->name : (dev->class ? dev->class->name : "");
}

__printf(2, 3) int dev_set_name(struct device *dev, const char *name, ...);

static inline int dev_to_node(struct device *dev)
{
	return NUMA_NO_NODE;
}
static inline void set_dev_node(struct device *dev, int node)
{
}

static inline void *dev_get_drvdata(const struct device *dev)
{
	return dev->driver_data;
}

static inline void dev_set_drvdata(struct device *dev, void *data)
{
	dev->driver_data = data;
}


static inline void dev_set_uevent_suppress(struct device *dev, int val)
{
	dev->kobj.uevent_suppress = val;
}

static inline int device_is_registered(struct device *dev)
{
	return dev->kobj.state_in_sysfs;
}

static inline void device_set_pm_not_required(struct device *dev)
{
	dev->power.no_pm = true;
}

static inline void dev_pm_set_driver_flags(struct device *dev, u32 flags)
{
	dev->power.driver_flags = flags;
}


static inline void device_lock(struct device *dev)
{
	mutex_lock(&dev->mutex);
}


static inline void device_unlock(struct device *dev)
{
	mutex_unlock(&dev->mutex);
}

static inline void device_lock_assert(struct device *dev)
{
	lockdep_assert_held(&dev->mutex);
}

int __must_check device_register(struct device *dev);
void device_unregister(struct device *dev);
void device_initialize(struct device *dev);
int __must_check device_add(struct device *dev);
void device_del(struct device *dev);
/* device_for_each_child, device_find_child, device_get_devnode removed - never called */


/* lock_device_hotplug, unlock_device_hotplug, lock_device_hotplug_sysfs,
   device_offline, device_online, set_primary_fwnode, set_secondary_fwnode,
   device_set_of_node_from_dev, device_set_node, __root_device_register,
   root_device_unregister removed - unused */

int __must_check device_driver_attach(struct device_driver *drv,
				      struct device *dev);
int __must_check device_bind_driver(struct device *dev);
void device_release_driver(struct device *dev);
int  __must_check device_attach(struct device *dev);
int __must_check driver_attach(struct device_driver *drv);
void device_initial_probe(struct device *dev);
int __must_check device_reprobe(struct device *dev);

bool device_is_bound(struct device *dev);

__printf(5, 6) struct device *
device_create(struct class *cls, struct device *parent, dev_t devt,
	      void *drvdata, const char *fmt, ...);
__printf(6, 7) struct device *
device_create_with_groups(struct class *cls, struct device *parent, dev_t devt,
			  void *drvdata, const struct attribute_group **groups,
			  const char *fmt, ...);
void device_destroy(struct class *cls, dev_t devt);

int __must_check device_add_groups(struct device *dev,
				   const struct attribute_group **groups);
void device_remove_groups(struct device *dev,
			  const struct attribute_group **groups);

static inline void device_remove_group(struct device *dev,
				       const struct attribute_group *grp)
{
	const struct attribute_group *groups[] = { grp, NULL };

	return device_remove_groups(dev, groups);
}



struct device *get_device(struct device *dev);
void put_device(struct device *dev);
/* kill_device removed - now static in core.c */

static inline int devtmpfs_mount(void) { return 0; }

void device_shutdown(void);

const char *dev_driver_string(const struct device *dev);

/* device_link_add, device_link_del, device_link_remove,
   device_links_supplier_sync_state_pause, device_links_supplier_sync_state_resume removed - unused */

extern __printf(3, 4)
int dev_err_probe(const struct device *dev, int err, const char *fmt, ...);

#define MODULE_ALIAS_CHARDEV(major,minor) \
	MODULE_ALIAS("char-major-" __stringify(major) "-" __stringify(minor))
#define MODULE_ALIAS_CHARDEV_MAJOR(major) \
	MODULE_ALIAS("char-major-" __stringify(major) "-*")

#define sysfs_deprecated 0

#endif  
