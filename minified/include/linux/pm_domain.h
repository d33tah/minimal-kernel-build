
#ifndef _LINUX_PM_DOMAIN_H
#define _LINUX_PM_DOMAIN_H

#include <linux/device.h>
#include <linux/ktime.h>
#include <linux/mutex.h>
#include <linux/pm.h>
#include <linux/err.h>
#include <linux/of.h>
#include <linux/notifier.h>
#include <linux/spinlock.h>
#include <linux/cpumask.h>

/* GENPD_FLAG_*, gpd_status, genpd_notication - not used in minimal kernel */
enum gpd_status { GENPD_STATE_OFF };
enum genpd_notication { GENPD_NOTIFY_ON };

struct dev_power_governor {
	bool (*power_down_ok)(struct dev_pm_domain *domain);
	bool (*suspend_ok)(struct device *dev);
};

struct gpd_dev_ops {
	int (*start)(struct device *dev);
	int (*stop)(struct device *dev);
};

struct genpd_governor_data {
	s64 max_off_time_ns;
	bool max_off_time_changed;
	ktime_t next_wakeup;
	bool cached_power_down_ok;
	bool cached_power_down_state_idx;
};

struct genpd_power_state {
	s64 power_off_latency_ns;
	s64 power_on_latency_ns;
	s64 residency_ns;
	u64 usage;
	u64 rejected;
	struct fwnode_handle *fwnode;
	u64 idle_time;
	void *data;
};

struct genpd_lock_ops;
struct dev_pm_opp;
struct opp_table;

struct generic_pm_domain {
	struct device dev;
	struct dev_pm_domain domain;	 
	struct list_head gpd_list_node;	 
	struct list_head parent_links;	 
	struct list_head child_links;	 
	struct list_head dev_list;	 
	struct dev_power_governor *gov;
	struct genpd_governor_data *gd;	 
	struct work_struct power_off_work;
	struct fwnode_handle *provider;	 
	bool has_provider;
	const char *name;
	atomic_t sd_count;	 
	enum gpd_status status;	 
	unsigned int device_count;	 
	unsigned int suspended_count;	 
	unsigned int prepared_count;	 
	unsigned int performance_state;	 
	cpumask_var_t cpus;		 
	int (*power_off)(struct generic_pm_domain *domain);
	int (*power_on)(struct generic_pm_domain *domain);
	struct raw_notifier_head power_notifiers;  
	struct opp_table *opp_table;	 
	unsigned int (*opp_to_performance_state)(struct generic_pm_domain *genpd,
						 struct dev_pm_opp *opp);
	int (*set_performance_state)(struct generic_pm_domain *genpd,
				     unsigned int state);
	struct gpd_dev_ops dev_ops;
	int (*attach_dev)(struct generic_pm_domain *domain,
			  struct device *dev);
	void (*detach_dev)(struct generic_pm_domain *domain,
			   struct device *dev);
	unsigned int flags;		 
	struct genpd_power_state *states;
	void (*free_states)(struct genpd_power_state *states,
			    unsigned int state_count);
	unsigned int state_count;  
	unsigned int state_idx;  
	u64 on_time;
	u64 accounting_time;
	const struct genpd_lock_ops *lock_ops;
	union {
		struct mutex mlock;
		struct {
			spinlock_t slock;
			unsigned long lock_flags;
		};
	};

};

static inline struct generic_pm_domain *pd_to_genpd(struct dev_pm_domain *pd)
{
	return container_of(pd, struct generic_pm_domain, domain);
}

struct gpd_link {
	struct generic_pm_domain *parent;
	struct list_head parent_node;
	struct generic_pm_domain *child;
	struct list_head child_node;

	 
	unsigned int performance_state;
	unsigned int prev_performance_state;
};

struct gpd_timing_data {
	s64 suspend_latency_ns;
	s64 resume_latency_ns;
	s64 effective_constraint_ns;
	ktime_t	next_wakeup;
	bool constraint_changed;
	bool cached_suspend_ok;
};

struct pm_domain_data {
	struct list_head list_node;
	struct device *dev;
};

struct generic_pm_domain_data {
	struct pm_domain_data base;
	struct gpd_timing_data *td;
	struct notifier_block nb;
	struct notifier_block *power_nb;
	int cpu;
	unsigned int performance_state;
	unsigned int default_pstate;
	unsigned int rpm_pstate;
	void *data;
};


static inline struct generic_pm_domain_data *dev_gpd_data(struct device *dev)
{
	return ERR_PTR(-ENOSYS);
}
static inline int pm_genpd_add_device(struct generic_pm_domain *genpd,
				      struct device *dev)
{
	return -ENOSYS;
}
static inline int pm_genpd_remove_device(struct device *dev)
{
	return -ENOSYS;
}
static inline int pm_genpd_add_subdomain(struct generic_pm_domain *genpd,
					 struct generic_pm_domain *subdomain)
{
	return -ENOSYS;
}
static inline int pm_genpd_remove_subdomain(struct generic_pm_domain *genpd,
					    struct generic_pm_domain *subdomain)
{
	return -ENOSYS;
}
static inline int pm_genpd_init(struct generic_pm_domain *genpd,
				struct dev_power_governor *gov, bool is_off)
{
	return -ENOSYS;
}
static inline int pm_genpd_remove(struct generic_pm_domain *genpd)
{
	return -EOPNOTSUPP;
}

static inline int dev_pm_genpd_set_performance_state(struct device *dev,
						     unsigned int state)
{
	return -EOPNOTSUPP;
}

static inline int dev_pm_genpd_add_notifier(struct device *dev,
					    struct notifier_block *nb)
{
	return -EOPNOTSUPP;
}

static inline int dev_pm_genpd_remove_notifier(struct device *dev)
{
	return -EOPNOTSUPP;
}

static inline void dev_pm_genpd_set_next_wakeup(struct device *dev, ktime_t next)
{ }

#define simple_qos_governor		(*(struct dev_power_governor *)(NULL))
#define pm_domain_always_on_gov		(*(struct dev_power_governor *)(NULL))

static inline void dev_pm_genpd_suspend(struct device *dev) {}
static inline void dev_pm_genpd_resume(struct device *dev) {}

struct of_device_id;

typedef struct generic_pm_domain *(*genpd_xlate_t)(struct of_phandle_args *args,
						   void *data);

struct genpd_onecell_data {
	struct generic_pm_domain **domains;
	unsigned int num_domains;
	genpd_xlate_t xlate;
};

static inline int of_genpd_add_provider_simple(struct device_node *np,
					struct generic_pm_domain *genpd)
{
	return -EOPNOTSUPP;
}

static inline int of_genpd_add_provider_onecell(struct device_node *np,
					struct genpd_onecell_data *data)
{
	return -EOPNOTSUPP;
}

static inline void of_genpd_del_provider(struct device_node *np) {}

static inline int of_genpd_add_device(struct of_phandle_args *args,
				      struct device *dev)
{
	return -ENODEV;
}

static inline int of_genpd_add_subdomain(struct of_phandle_args *parent_spec,
					 struct of_phandle_args *subdomain_spec)
{
	return -ENODEV;
}

static inline int of_genpd_remove_subdomain(struct of_phandle_args *parent_spec,
					struct of_phandle_args *subdomain_spec)
{
	return -ENODEV;
}

static inline int of_genpd_parse_idle_states(struct device_node *dn,
			struct genpd_power_state **states, int *n)
{
	return -ENODEV;
}

static inline unsigned int
pm_genpd_opp_to_performance_state(struct device *genpd_dev,
				  struct dev_pm_opp *opp)
{
	return 0;
}

static inline int genpd_dev_pm_attach(struct device *dev)
{
	return 0;
}

static inline struct device *genpd_dev_pm_attach_by_id(struct device *dev,
						       unsigned int index)
{
	return NULL;
}

static inline struct device *genpd_dev_pm_attach_by_name(struct device *dev,
							 const char *name)
{
	return NULL;
}

static inline
struct generic_pm_domain *of_genpd_remove_last(struct device_node *np)
{
	return ERR_PTR(-EOPNOTSUPP);
}

static inline int dev_pm_domain_attach(struct device *dev, bool power_on)
{
	return 0;
}
static inline struct device *dev_pm_domain_attach_by_id(struct device *dev,
							unsigned int index)
{
	return NULL;
}
static inline struct device *dev_pm_domain_attach_by_name(struct device *dev,
							  const char *name)
{
	return NULL;
}
static inline void dev_pm_domain_detach(struct device *dev, bool power_off) {}
static inline int dev_pm_domain_start(struct device *dev)
{
	return 0;
}
static inline void dev_pm_domain_set(struct device *dev,
				     struct dev_pm_domain *pd) {}

#endif  
