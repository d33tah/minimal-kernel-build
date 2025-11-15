 
 

#ifndef _DEVICE_H_
#define _DEVICE_H_

#include <linux/dev_printk.h>

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
#include <asm/device.h>

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

int subsys_interface_register(struct subsys_interface *sif);
void subsys_interface_unregister(struct subsys_interface *sif);

int subsys_system_register(struct bus_type *subsys,
			   const struct attribute_group **groups);
int subsys_virtual_register(struct bus_type *subsys,
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

struct dev_ext_attribute {
	struct device_attribute attr;
	void *var;
};

ssize_t device_show_ulong(struct device *dev, struct device_attribute *attr,
			  char *buf);
ssize_t device_store_ulong(struct device *dev, struct device_attribute *attr,
			   const char *buf, size_t count);
ssize_t device_show_int(struct device *dev, struct device_attribute *attr,
			char *buf);
ssize_t device_store_int(struct device *dev, struct device_attribute *attr,
			 const char *buf, size_t count);
ssize_t device_show_bool(struct device *dev, struct device_attribute *attr,
			char *buf);
ssize_t device_store_bool(struct device *dev, struct device_attribute *attr,
			 const char *buf, size_t count);

#define DEVICE_ATTR(_name, _mode, _show, _store) \
	struct device_attribute dev_attr_##_name = __ATTR(_name, _mode, _show, _store)
#define DEVICE_ATTR_PREALLOC(_name, _mode, _show, _store) \
	struct device_attribute dev_attr_##_name = \
		__ATTR_PREALLOC(_name, _mode, _show, _store)
#define DEVICE_ATTR_RW(_name) \
	struct device_attribute dev_attr_##_name = __ATTR_RW(_name)
#define DEVICE_ATTR_ADMIN_RW(_name) \
	struct device_attribute dev_attr_##_name = __ATTR_RW_MODE(_name, 0600)
#define DEVICE_ATTR_RO(_name) \
	struct device_attribute dev_attr_##_name = __ATTR_RO(_name)
#define DEVICE_ATTR_ADMIN_RO(_name) \
	struct device_attribute dev_attr_##_name = __ATTR_RO_MODE(_name, 0400)
#define DEVICE_ATTR_WO(_name) \
	struct device_attribute dev_attr_##_name = __ATTR_WO(_name)
#define DEVICE_ULONG_ATTR(_name, _mode, _var) \
	struct dev_ext_attribute dev_attr_##_name = \
		{ __ATTR(_name, _mode, device_show_ulong, device_store_ulong), &(_var) }
#define DEVICE_INT_ATTR(_name, _mode, _var) \
	struct dev_ext_attribute dev_attr_##_name = \
		{ __ATTR(_name, _mode, device_show_int, device_store_int), &(_var) }
#define DEVICE_BOOL_ATTR(_name, _mode, _var) \
	struct dev_ext_attribute dev_attr_##_name = \
		{ __ATTR(_name, _mode, device_show_bool, device_store_bool), &(_var) }
#define DEVICE_ATTR_IGNORE_LOCKDEP(_name, _mode, _show, _store) \
	struct device_attribute dev_attr_##_name =		\
		__ATTR_IGNORE_LOCKDEP(_name, _mode, _show, _store)

int device_create_file(struct device *device,
		       const struct device_attribute *entry);
void device_remove_file(struct device *dev,
			const struct device_attribute *attr);
bool device_remove_file_self(struct device *dev,
			     const struct device_attribute *attr);
int __must_check device_create_bin_file(struct device *dev,
					const struct bin_attribute *attr);
void device_remove_bin_file(struct device *dev,
			    const struct bin_attribute *attr);

 
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

 
void * __must_check devres_open_group(struct device *dev, void *id, gfp_t gfp);
void devres_close_group(struct device *dev, void *id);
void devres_remove_group(struct device *dev, void *id);
int devres_release_group(struct device *dev, void *id);

 
void *devm_kmalloc(struct device *dev, size_t size, gfp_t gfp) __malloc;
void *devm_krealloc(struct device *dev, void *ptr, size_t size,
		    gfp_t gfp) __must_check;
__printf(3, 0) char *devm_kvasprintf(struct device *dev, gfp_t gfp,
				     const char *fmt, va_list ap) __malloc;
__printf(3, 4) char *devm_kasprintf(struct device *dev, gfp_t gfp,
				    const char *fmt, ...) __malloc;
void devm_kfree(struct device *dev, const void *p);
char *devm_kstrdup(struct device *dev, const char *s, gfp_t gfp) __malloc;
const char *devm_kstrdup_const(struct device *dev, const char *s, gfp_t gfp);
void *devm_kmemdup(struct device *dev, const void *src, size_t len, gfp_t gfp);

unsigned long devm_get_free_pages(struct device *dev,
				  gfp_t gfp_mask, unsigned int order);
void devm_free_pages(struct device *dev, unsigned long addr);

void __iomem *devm_ioremap_resource(struct device *dev,
				    const struct resource *res);
void __iomem *devm_ioremap_resource_wc(struct device *dev,
				       const struct resource *res);

void __iomem *devm_of_iomap(struct device *dev,
			    struct device_node *node, int index,
			    resource_size_t *size);


int devm_add_action(struct device *dev, void (*action)(void *), void *data);
void devm_remove_action(struct device *dev, void (*action)(void *), void *data);
void devm_release_action(struct device *dev, void (*action)(void *), void *data);

static inline int devm_add_action_or_reset(struct device *dev,
					   void (*action)(void *), void *data)
{
	int ret;

	ret = devm_add_action(dev, action, data);
	if (ret)
		action(data);

	return ret;
}

 
#define devm_alloc_percpu(dev, type)      \
	((typeof(type) __percpu *)__devm_alloc_percpu((dev), sizeof(type), \
						      __alignof__(type)))

void __percpu *__devm_alloc_percpu(struct device *dev, size_t size,
				   size_t align);
void devm_free_percpu(struct device *dev, void __percpu *pdata);

struct device_dma_parameters {
	 
	unsigned int max_segment_size;
	unsigned int min_align_mask;
	unsigned long segment_boundary_mask;
};

 
enum device_link_state {
	DL_STATE_NONE = -1,
	DL_STATE_DORMANT = 0,
	DL_STATE_AVAILABLE,
	DL_STATE_CONSUMER_PROBE,
	DL_STATE_ACTIVE,
	DL_STATE_SUPPLIER_UNBIND,
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
	DL_DEV_DRIVER_BOUND,
	DL_DEV_UNBINDING,
};

 
enum device_removable {
	DEVICE_REMOVABLE_NOT_SUPPORTED = 0,  
	DEVICE_REMOVABLE_UNKNOWN,
	DEVICE_FIXED,
	DEVICE_REMOVABLE,
};

 
struct dev_links_info {
	struct list_head suppliers;
	struct list_head consumers;
	struct list_head defer_sync;
	enum dl_dev_state status;
};

 
struct dev_msi_info {
};

 
enum device_physical_location_panel {
	DEVICE_PANEL_TOP,
	DEVICE_PANEL_BOTTOM,
	DEVICE_PANEL_LEFT,
	DEVICE_PANEL_RIGHT,
	DEVICE_PANEL_FRONT,
	DEVICE_PANEL_BACK,
	DEVICE_PANEL_UNKNOWN,
};

 
enum device_physical_location_vertical_position {
	DEVICE_VERT_POS_UPPER,
	DEVICE_VERT_POS_CENTER,
	DEVICE_VERT_POS_LOWER,
};

 
enum device_physical_location_horizontal_position {
	DEVICE_HORI_POS_LEFT,
	DEVICE_HORI_POS_CENTER,
	DEVICE_HORI_POS_RIGHT,
};

 
struct device_physical_location {
	enum device_physical_location_panel panel;
	enum device_physical_location_vertical_position vertical_position;
	enum device_physical_location_horizontal_position horizontal_position;
	bool dock;
	bool lid;
};

 
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



#include <linux/pm_wakeup.h>

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

static inline bool device_pm_not_required(struct device *dev)
{
	return dev->power.no_pm;
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

static inline struct device_node *dev_of_node(struct device *dev)
{
	if (!IS_ENABLED(CONFIG_OF) || !dev)
		return NULL;
	return dev->of_node;
}

static inline bool dev_has_sync_state(struct device *dev)
{
	if (!dev)
		return false;
	if (dev->driver && dev->driver->sync_state)
		return true;
	if (dev->bus && dev->bus->sync_state)
		return true;
	return false;
}

static inline bool dev_removable_is_valid(struct device *dev)
{
	return dev->removable != DEVICE_REMOVABLE_NOT_SUPPORTED;
}

 
int __must_check device_register(struct device *dev);
void device_unregister(struct device *dev);
void device_initialize(struct device *dev);
int __must_check device_add(struct device *dev);
void device_del(struct device *dev);
int device_for_each_child(struct device *dev, void *data,
			  int (*fn)(struct device *dev, void *data));
int device_for_each_child_reverse(struct device *dev, void *data,
				  int (*fn)(struct device *dev, void *data));
struct device *device_find_child(struct device *dev, void *data,
				 int (*match)(struct device *dev, void *data));
struct device *device_find_child_by_name(struct device *parent,
					 const char *name);
int device_rename(struct device *dev, const char *new_name);
int device_move(struct device *dev, struct device *new_parent,
		enum dpm_order dpm_order);
int device_change_owner(struct device *dev, kuid_t kuid, kgid_t kgid);
const char *device_get_devnode(struct device *dev, umode_t *mode, kuid_t *uid,
			       kgid_t *gid, const char **tmp);
int device_is_dependent(struct device *dev, void *target);

static inline bool device_supports_offline(struct device *dev)
{
	return dev->bus && dev->bus->offline && dev->bus->online;
}

#define __device_lock_set_class(dev, name, key)                        \
do {                                                                   \
	struct device *__d2 __maybe_unused = dev;                      \
	lock_set_class(&__d2->mutex.dep_map, name, key, 0, _THIS_IP_); \
} while (0)

 
#define device_lock_set_class(dev, key) __device_lock_set_class(dev, #key, key)

 
#define device_lock_reset_class(dev) \
do { \
	struct device *__d __maybe_unused = dev;                       \
	lock_set_novalidate_class(&__d->mutex.dep_map, "&dev->mutex",  \
				  _THIS_IP_);                          \
} while (0)

void lock_device_hotplug(void);
void unlock_device_hotplug(void);
int lock_device_hotplug_sysfs(void);
int device_offline(struct device *dev);
int device_online(struct device *dev);
void set_primary_fwnode(struct device *dev, struct fwnode_handle *fwnode);
void set_secondary_fwnode(struct device *dev, struct fwnode_handle *fwnode);
void device_set_of_node_from_dev(struct device *dev, const struct device *dev2);
void device_set_node(struct device *dev, struct fwnode_handle *fwnode);


 
struct device *__root_device_register(const char *name, struct module *owner);

 
#define root_device_register(name) \
	__root_device_register(name, THIS_MODULE)

void root_device_unregister(struct device *root);

 
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

static inline int __must_check device_add_group(struct device *dev,
					const struct attribute_group *grp)
{
	const struct attribute_group *groups[] = { grp, NULL };

	return device_add_groups(dev, groups);
}

static inline void device_remove_group(struct device *dev,
				       const struct attribute_group *grp)
{
	const struct attribute_group *groups[] = { grp, NULL };

	return device_remove_groups(dev, groups);
}

int __must_check devm_device_add_groups(struct device *dev,
					const struct attribute_group **groups);
void devm_device_remove_groups(struct device *dev,
			       const struct attribute_group **groups);
int __must_check devm_device_add_group(struct device *dev,
				       const struct attribute_group *grp);
void devm_device_remove_group(struct device *dev,
			      const struct attribute_group *grp);

 
 
extern int (*platform_notify)(struct device *dev);

extern int (*platform_notify_remove)(struct device *dev);


 
struct device *get_device(struct device *dev);
void put_device(struct device *dev);
bool kill_device(struct device *dev);

static inline int devtmpfs_mount(void) { return 0; }

 
void device_shutdown(void);

 
const char *dev_driver_string(const struct device *dev);

 
struct device_link *device_link_add(struct device *consumer,
				    struct device *supplier, u32 flags);
void device_link_del(struct device_link *link);
void device_link_remove(void *consumer, struct device *supplier);
void device_links_supplier_sync_state_pause(void);
void device_links_supplier_sync_state_resume(void);

extern __printf(3, 4)
int dev_err_probe(const struct device *dev, int err, const char *fmt, ...);

 
#define MODULE_ALIAS_CHARDEV(major,minor) \
	MODULE_ALIAS("char-major-" __stringify(major) "-" __stringify(minor))
#define MODULE_ALIAS_CHARDEV_MAJOR(major) \
	MODULE_ALIAS("char-major-" __stringify(major) "-*")

#define sysfs_deprecated 0

#endif  
