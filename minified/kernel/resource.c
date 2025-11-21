 
 

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/export.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <linux/pseudo_fs.h>
#include <linux/sched.h>
#include <linux/seq_file.h>
#include <linux/device.h>
#include <linux/pfn.h>
#include <linux/mm.h>
#include <linux/mount.h>
#include <linux/resource_ext.h>
#include <uapi/linux/magic.h>
#include <asm/io.h>


struct resource ioport_resource = {
	.name	= "PCI IO",
	.start	= 0,
	.end	= IO_SPACE_LIMIT,
	.flags	= IORESOURCE_IO,
};

struct resource iomem_resource = {
	.name	= "PCI mem",
	.start	= 0,
	.end	= -1,
	.flags	= IORESOURCE_MEM,
};

 
struct resource_constraint {
	resource_size_t min, max, align;
	resource_size_t (*alignf)(void *, const struct resource *,
			resource_size_t, resource_size_t);
	void *alignf_data;
};

static DEFINE_RWLOCK(resource_lock);

static struct resource *next_resource(struct resource *p)
{
	if (p->child)
		return p->child;
	while (!p->sibling && p->parent)
		p = p->parent;
	return p->sibling;
}

static struct resource *next_resource_skip_children(struct resource *p)
{
	while (!p->sibling && p->parent)
		p = p->parent;
	return p->sibling;
}

#define for_each_resource(_root, _p, _skip_children) \
	for ((_p) = (_root)->child; (_p); \
	     (_p) = (_skip_children) ? next_resource_skip_children(_p) : \
				       next_resource(_p))

static void *r_next(struct seq_file *m, void *v, loff_t *pos)
{
	struct resource *p = v;
	(*pos)++;
	return (void *)next_resource(p);
}


static void free_resource(struct resource *res)
{
	 
	if (res && PageSlab(virt_to_head_page(res)))
		kfree(res);
}

static struct resource *alloc_resource(gfp_t flags)
{
	return kzalloc(sizeof(struct resource), flags);
}

 
static struct resource * __request_resource(struct resource *root, struct resource *new)
{
	resource_size_t start = new->start;
	resource_size_t end = new->end;
	struct resource *tmp, **p;

	if (end < start)
		return root;
	if (start < root->start)
		return root;
	if (end > root->end)
		return root;
	p = &root->child;
	for (;;) {
		tmp = *p;
		if (!tmp || tmp->start > end) {
			new->sibling = tmp;
			*p = new;
			new->parent = root;
			return NULL;
		}
		p = &tmp->sibling;
		if (tmp->end < start)
			continue;
		return tmp;
	}
}

static int __release_resource(struct resource *old, bool release_child)
{
	struct resource *tmp, **p, *chd;

	p = &old->parent->child;
	for (;;) {
		tmp = *p;
		if (!tmp)
			break;
		if (tmp == old) {
			if (release_child || !(tmp->child)) {
				*p = tmp->sibling;
			} else {
				for (chd = tmp->child;; chd = chd->sibling) {
					chd->parent = tmp->parent;
					if (!(chd->sibling))
						break;
				}
				*p = tmp->child;
				chd->sibling = tmp->sibling;
			}
			old->parent = NULL;
			return 0;
		}
		p = &tmp->sibling;
	}
	return -EINVAL;
}

static void __release_child_resources(struct resource *r)
{
	struct resource *tmp, *p;
	resource_size_t size;

	p = r->child;
	r->child = NULL;
	while (p) {
		tmp = p;
		p = p->sibling;

		tmp->parent = NULL;
		tmp->sibling = NULL;
		__release_child_resources(tmp);

		 
		size = resource_size(tmp);
		tmp->start = 0;
		tmp->end = size - 1;
	}
}

void release_child_resources(struct resource *r)
{
	write_lock(&resource_lock);
	__release_child_resources(r);
	write_unlock(&resource_lock);
}

 
struct resource *request_resource_conflict(struct resource *root, struct resource *new)
{
	struct resource *conflict;

	write_lock(&resource_lock);
	conflict = __request_resource(root, new);
	write_unlock(&resource_lock);
	return conflict;
}

 
int request_resource(struct resource *root, struct resource *new)
{
	struct resource *conflict;

	conflict = request_resource_conflict(root, new);
	return conflict ? -EBUSY : 0;
}


 
int release_resource(struct resource *old)
{
	int retval;

	write_lock(&resource_lock);
	retval = __release_resource(old, true);
	write_unlock(&resource_lock);
	return retval;
}


 
static int find_next_iomem_res(resource_size_t start, resource_size_t end,
			       unsigned long flags, unsigned long desc,
			       struct resource *res)
{
	struct resource *p;

	if (!res)
		return -EINVAL;

	if (start >= end)
		return -EINVAL;

	read_lock(&resource_lock);

	for (p = iomem_resource.child; p; p = next_resource(p)) {
		 
		if (p->start > end) {
			p = NULL;
			break;
		}

		 
		if (p->end < start)
			continue;

		if ((p->flags & flags) != flags)
			continue;
		if ((desc != IORES_DESC_NONE) && (desc != p->desc))
			continue;

		 
		break;
	}

	if (p) {
		 
		*res = (struct resource) {
			.start = max(start, p->start),
			.end = min(end, p->end),
			.flags = p->flags,
			.desc = p->desc,
			.parent = p->parent,
		};
	}

	read_unlock(&resource_lock);
	return p ? 0 : -ENODEV;
}

static int __walk_iomem_res_desc(resource_size_t start, resource_size_t end,
				 unsigned long flags, unsigned long desc,
				 void *arg,
				 int (*func)(struct resource *, void *))
{
	struct resource res;
	int ret = -EINVAL;

	while (start < end &&
	       !find_next_iomem_res(start, end, flags, desc, &res)) {
		ret = (*func)(&res, arg);
		if (ret)
			break;

		start = res.end + 1;
	}

	return ret;
}

 
int walk_iomem_res_desc(unsigned long desc, unsigned long flags, u64 start,
		u64 end, void *arg, int (*func)(struct resource *, void *))
{
	return __walk_iomem_res_desc(start, end, flags, desc, arg, func);
}

 
int walk_system_ram_res(u64 start, u64 end, void *arg,
			int (*func)(struct resource *, void *))
{
	unsigned long flags = IORESOURCE_SYSTEM_RAM | IORESOURCE_BUSY;

	return __walk_iomem_res_desc(start, end, flags, IORES_DESC_NONE, arg,
				     func);
}

 
int walk_mem_res(u64 start, u64 end, void *arg,
		 int (*func)(struct resource *, void *))
{
	unsigned long flags = IORESOURCE_MEM | IORESOURCE_BUSY;

	return __walk_iomem_res_desc(start, end, flags, IORES_DESC_NONE, arg,
				     func);
}

 
int walk_system_ram_range(unsigned long start_pfn, unsigned long nr_pages,
			  void *arg, int (*func)(unsigned long, unsigned long, void *))
{
	resource_size_t start, end;
	unsigned long flags;
	struct resource res;
	unsigned long pfn, end_pfn;
	int ret = -EINVAL;

	start = (u64) start_pfn << PAGE_SHIFT;
	end = ((u64)(start_pfn + nr_pages) << PAGE_SHIFT) - 1;
	flags = IORESOURCE_SYSTEM_RAM | IORESOURCE_BUSY;
	while (start < end &&
	       !find_next_iomem_res(start, end, flags, IORES_DESC_NONE, &res)) {
		pfn = PFN_UP(res.start);
		end_pfn = PFN_DOWN(res.end + 1);
		if (end_pfn > pfn)
			ret = (*func)(pfn, end_pfn - pfn, arg);
		if (ret)
			break;
		start = res.end + 1;
	}
	return ret;
}

static int __is_ram(unsigned long pfn, unsigned long nr_pages, void *arg)
{
	return 1;
}

 
int __weak page_is_ram(unsigned long pfn)
{
	return walk_system_ram_range(pfn, 1, NULL, __is_ram) == 1;
}

static int __region_intersects(resource_size_t start, size_t size,
			unsigned long flags, unsigned long desc)
{
	struct resource res;
	int type = 0; int other = 0;
	struct resource *p;

	res.start = start;
	res.end = start + size - 1;

	for (p = iomem_resource.child; p ; p = p->sibling) {
		bool is_type = (((p->flags & flags) == flags) &&
				((desc == IORES_DESC_NONE) ||
				 (desc == p->desc)));

		if (resource_overlaps(p, &res))
			is_type ? type++ : other++;
	}

	if (type == 0)
		return REGION_DISJOINT;

	if (other == 0)
		return REGION_INTERSECTS;

	return REGION_MIXED;
}

 
int region_intersects(resource_size_t start, size_t size, unsigned long flags,
		      unsigned long desc)
{
	int ret;

	read_lock(&resource_lock);
	ret = __region_intersects(start, size, flags, desc);
	read_unlock(&resource_lock);

	return ret;
}

void __weak arch_remove_reservations(struct resource *avail)
{
}

static resource_size_t simple_align_resource(void *data,
					     const struct resource *avail,
					     resource_size_t size,
					     resource_size_t align)
{
	return avail->start;
}

static void resource_clip(struct resource *res, resource_size_t min,
			  resource_size_t max)
{
	if (res->start < min)
		res->start = min;
	if (res->end > max)
		res->end = max;
}

 
static int __find_resource(struct resource *root, struct resource *old,
			 struct resource *new,
			 resource_size_t  size,
			 struct resource_constraint *constraint)
{
	struct resource *this = root->child;
	struct resource tmp = *new, avail, alloc;

	tmp.start = root->start;
	 
	if (this && this->start == root->start) {
		tmp.start = (this == old) ? old->start : this->end + 1;
		this = this->sibling;
	}
	for(;;) {
		if (this)
			tmp.end = (this == old) ?  this->end : this->start - 1;
		else
			tmp.end = root->end;

		if (tmp.end < tmp.start)
			goto next;

		resource_clip(&tmp, constraint->min, constraint->max);
		arch_remove_reservations(&tmp);

		 
		avail.start = ALIGN(tmp.start, constraint->align);
		avail.end = tmp.end;
		avail.flags = new->flags & ~IORESOURCE_UNSET;
		if (avail.start >= tmp.start) {
			alloc.flags = avail.flags;
			alloc.start = constraint->alignf(constraint->alignf_data, &avail,
					size, constraint->align);
			alloc.end = alloc.start + size - 1;
			if (alloc.start <= alloc.end &&
			    resource_contains(&avail, &alloc)) {
				new->start = alloc.start;
				new->end = alloc.end;
				return 0;
			}
		}

next:		if (!this || this->end == root->end)
			break;

		if (this != old)
			tmp.start = this->end + 1;
		this = this->sibling;
	}
	return -EBUSY;
}

 
static int find_resource(struct resource *root, struct resource *new,
			resource_size_t size,
			struct resource_constraint  *constraint)
{
	return  __find_resource(root, NULL, new, size, constraint);
}

 
static int reallocate_resource(struct resource *root, struct resource *old,
			       resource_size_t newsize,
			       struct resource_constraint *constraint)
{
	int err=0;
	struct resource new = *old;
	struct resource *conflict;

	write_lock(&resource_lock);

	if ((err = __find_resource(root, old, &new, newsize, constraint)))
		goto out;

	if (resource_contains(&new, old)) {
		old->start = new.start;
		old->end = new.end;
		goto out;
	}

	if (old->child) {
		err = -EBUSY;
		goto out;
	}

	if (resource_contains(old, &new)) {
		old->start = new.start;
		old->end = new.end;
	} else {
		__release_resource(old, true);
		*old = new;
		conflict = __request_resource(root, old);
		BUG_ON(conflict);
	}
out:
	write_unlock(&resource_lock);
	return err;
}


 
int allocate_resource(struct resource *root, struct resource *new,
		      resource_size_t size, resource_size_t min,
		      resource_size_t max, resource_size_t align,
		      resource_size_t (*alignf)(void *,
						const struct resource *,
						resource_size_t,
						resource_size_t),
		      void *alignf_data)
{
	int err;
	struct resource_constraint constraint;

	if (!alignf)
		alignf = simple_align_resource;

	constraint.min = min;
	constraint.max = max;
	constraint.align = align;
	constraint.alignf = alignf;
	constraint.alignf_data = alignf_data;

	if ( new->parent ) {
		 
		return reallocate_resource(root, new, size, &constraint);
	}

	write_lock(&resource_lock);
	err = find_resource(root, new, size, &constraint);
	if (err >= 0 && __request_resource(root, new))
		err = -EBUSY;
	write_unlock(&resource_lock);
	return err;
}


 
struct resource *lookup_resource(struct resource *root, resource_size_t start)
{
	struct resource *res;

	read_lock(&resource_lock);
	for (res = root->child; res; res = res->sibling) {
		if (res->start == start)
			break;
	}
	read_unlock(&resource_lock);

	return res;
}

 
static struct resource * __insert_resource(struct resource *parent, struct resource *new)
{
	struct resource *first, *next;

	for (;; parent = first) {
		first = __request_resource(parent, new);
		if (!first)
			return first;

		if (first == parent)
			return first;
		if (WARN_ON(first == new))	 
			return first;

		if ((first->start > new->start) || (first->end < new->end))
			break;
		if ((first->start == new->start) && (first->end == new->end))
			break;
	}

	for (next = first; ; next = next->sibling) {
		 
		if (next->start < new->start || next->end > new->end)
			return next;
		if (!next->sibling)
			break;
		if (next->sibling->start > new->end)
			break;
	}

	new->parent = parent;
	new->sibling = next->sibling;
	new->child = first;

	next->sibling = NULL;
	for (next = first; next; next = next->sibling)
		next->parent = new;

	if (parent->child == first) {
		parent->child = new;
	} else {
		next = parent->child;
		while (next->sibling != first)
			next = next->sibling;
		next->sibling = new;
	}
	return NULL;
}

 
struct resource *insert_resource_conflict(struct resource *parent, struct resource *new)
{
	struct resource *conflict;

	write_lock(&resource_lock);
	conflict = __insert_resource(parent, new);
	write_unlock(&resource_lock);
	return conflict;
}

 
int insert_resource(struct resource *parent, struct resource *new)
{
	struct resource *conflict;

	conflict = insert_resource_conflict(parent, new);
	return conflict ? -EBUSY : 0;
}

 
void insert_resource_expand_to_fit(struct resource *root, struct resource *new)
{
	if (new->parent)
		return;

	write_lock(&resource_lock);
	for (;;) {
		struct resource *conflict;

		conflict = __insert_resource(root, new);
		if (!conflict)
			break;
		if (conflict == root)
			break;

		 
		if (conflict->start < new->start)
			new->start = conflict->start;
		if (conflict->end > new->end)
			new->end = conflict->end;
	}
	write_unlock(&resource_lock);
}

 
int remove_resource(struct resource *old)
{
	int retval;

	write_lock(&resource_lock);
	retval = __release_resource(old, false);
	write_unlock(&resource_lock);
	return retval;
}

static int __adjust_resource(struct resource *res, resource_size_t start,
				resource_size_t size)
{
	struct resource *tmp, *parent = res->parent;
	resource_size_t end = start + size - 1;
	int result = -EBUSY;

	if (!parent)
		goto skip;

	if ((start < parent->start) || (end > parent->end))
		goto out;

	if (res->sibling && (res->sibling->start <= end))
		goto out;

	tmp = parent->child;
	if (tmp != res) {
		while (tmp->sibling != res)
			tmp = tmp->sibling;
		if (start <= tmp->end)
			goto out;
	}

skip:
	for (tmp = res->child; tmp; tmp = tmp->sibling)
		if ((tmp->start < start) || (tmp->end > end))
			goto out;

	res->start = start;
	res->end = end;
	result = 0;

 out:
	return result;
}

 
int adjust_resource(struct resource *res, resource_size_t start,
		    resource_size_t size)
{
	int result;

	write_lock(&resource_lock);
	result = __adjust_resource(res, start, size);
	write_unlock(&resource_lock);
	return result;
}

static void __init
__reserve_region_with_split(struct resource *root, resource_size_t start,
			    resource_size_t end, const char *name)
{
	struct resource *parent = root;
	struct resource *conflict;
	struct resource *res = alloc_resource(GFP_ATOMIC);
	struct resource *next_res = NULL;
	int type = resource_type(root);

	if (!res)
		return;

	res->name = name;
	res->start = start;
	res->end = end;
	res->flags = type | IORESOURCE_BUSY;
	res->desc = IORES_DESC_NONE;

	while (1) {

		conflict = __request_resource(parent, res);
		if (!conflict) {
			if (!next_res)
				break;
			res = next_res;
			next_res = NULL;
			continue;
		}

		 
		if (conflict->start <= res->start &&
				conflict->end >= res->end) {
			free_resource(res);
			WARN_ON(next_res);
			break;
		}

		 
		if (conflict->start > res->start) {
			end = res->end;
			res->end = conflict->start - 1;
			if (conflict->end < end) {
				next_res = alloc_resource(GFP_ATOMIC);
				if (!next_res) {
					free_resource(res);
					break;
				}
				next_res->name = name;
				next_res->start = conflict->end + 1;
				next_res->end = end;
				next_res->flags = type | IORESOURCE_BUSY;
				next_res->desc = IORES_DESC_NONE;
			}
		} else {
			res->start = conflict->end + 1;
		}
	}

}

void __init
reserve_region_with_split(struct resource *root, resource_size_t start,
			  resource_size_t end, const char *name)
{
	int abort = 0;

	write_lock(&resource_lock);
	if (root->start > start || root->end < end) {
		if (start > root->end || end < root->start)
			abort = 1;
		else {
			if (end > root->end)
				end = root->end;
			if (start < root->start)
				start = root->start;
		}
	}
	if (!abort)
		__reserve_region_with_split(root, start, end, name);
	write_unlock(&resource_lock);
}

 
resource_size_t resource_alignment(struct resource *res)
{
	switch (res->flags & (IORESOURCE_SIZEALIGN | IORESOURCE_STARTALIGN)) {
	case IORESOURCE_SIZEALIGN:
		return resource_size(res);
	case IORESOURCE_STARTALIGN:
		return res->start;
	default:
		return 0;
	}
}

 

static DECLARE_WAIT_QUEUE_HEAD(muxed_resource_wait);

static struct inode *iomem_inode;

static void revoke_iomem(struct resource *res) {}

struct address_space *iomem_get_mapping(void)
{
	 
	return smp_load_acquire(&iomem_inode)->i_mapping;
}

static int __request_region_locked(struct resource *res, struct resource *parent,
				   resource_size_t start, resource_size_t n,
				   const char *name, int flags)
{
	DECLARE_WAITQUEUE(wait, current);

	res->name = name;
	res->start = start;
	res->end = start + n - 1;

	for (;;) {
		struct resource *conflict;

		res->flags = resource_type(parent) | resource_ext_type(parent);
		res->flags |= IORESOURCE_BUSY | flags;
		res->desc = parent->desc;

		conflict = __request_resource(parent, res);
		if (!conflict)
			break;
		 
		if (conflict->desc == IORES_DESC_DEVICE_PRIVATE_MEMORY) {
		}
		if (conflict != parent) {
			if (!(conflict->flags & IORESOURCE_BUSY)) {
				parent = conflict;
				continue;
			}
		}
		if (conflict->flags & flags & IORESOURCE_MUXED) {
			add_wait_queue(&muxed_resource_wait, &wait);
			write_unlock(&resource_lock);
			set_current_state(TASK_UNINTERRUPTIBLE);
			schedule();
			remove_wait_queue(&muxed_resource_wait, &wait);
			write_lock(&resource_lock);
			continue;
		}
		 
		return -EBUSY;
	}

	return 0;
}

 
struct resource *__request_region(struct resource *parent,
				  resource_size_t start, resource_size_t n,
				  const char *name, int flags)
{
	struct resource *res = alloc_resource(GFP_KERNEL);
	int ret;

	if (!res)
		return NULL;

	write_lock(&resource_lock);
	ret = __request_region_locked(res, parent, start, n, name, flags);
	write_unlock(&resource_lock);

	if (ret) {
		free_resource(res);
		return NULL;
	}

	if (parent == &iomem_resource)
		revoke_iomem(res);

	return res;
}

 
void __release_region(struct resource *parent, resource_size_t start,
		      resource_size_t n)
{
	struct resource **p;
	resource_size_t end;

	p = &parent->child;
	end = start + n - 1;

	write_lock(&resource_lock);

	for (;;) {
		struct resource *res = *p;

		if (!res)
			break;
		if (res->start <= start && res->end >= end) {
			if (!(res->flags & IORESOURCE_BUSY)) {
				p = &res->child;
				continue;
			}
			if (res->start != start || res->end != end)
				break;
			*p = res->sibling;
			write_unlock(&resource_lock);
			if (res->flags & IORESOURCE_MUXED)
				wake_up(&muxed_resource_wait);
			free_resource(res);
			return;
		}
		p = &res->sibling;
	}

	write_unlock(&resource_lock);

	printk(KERN_WARNING "Trying to free nonexistent resource "
		"<%016llx-%016llx>\n", (unsigned long long)start,
		(unsigned long long)end);
}



 
static void devm_resource_release(struct device *dev, void *ptr)
{
	struct resource **r = ptr;

	release_resource(*r);
}

 
int devm_request_resource(struct device *dev, struct resource *root,
			  struct resource *new)
{
	struct resource *conflict, **ptr;

	ptr = devres_alloc(devm_resource_release, sizeof(*ptr), GFP_KERNEL);
	if (!ptr)
		return -ENOMEM;

	*ptr = new;

	conflict = request_resource_conflict(root, new);
	if (conflict) {
		dev_err(dev, "resource collision: %pR conflicts with %s %pR\n",
			new, conflict->name, conflict);
		devres_free(ptr);
		return -EBUSY;
	}

	devres_add(dev, ptr);
	return 0;
}

static int devm_resource_match(struct device *dev, void *res, void *data)
{
	struct resource **ptr = res;

	return *ptr == data;
}

 
void devm_release_resource(struct device *dev, struct resource *new)
{
	WARN_ON(devres_release(dev, devm_resource_release, devm_resource_match,
			       new));
}

struct region_devres {
	struct resource *parent;
	resource_size_t start;
	resource_size_t n;
};

static void devm_region_release(struct device *dev, void *res)
{
	struct region_devres *this = res;

	__release_region(this->parent, this->start, this->n);
}

static int devm_region_match(struct device *dev, void *res, void *match_data)
{
	struct region_devres *this = res, *match = match_data;

	return this->parent == match->parent &&
		this->start == match->start && this->n == match->n;
}

struct resource *
__devm_request_region(struct device *dev, struct resource *parent,
		      resource_size_t start, resource_size_t n, const char *name)
{
	struct region_devres *dr = NULL;
	struct resource *res;

	dr = devres_alloc(devm_region_release, sizeof(struct region_devres),
			  GFP_KERNEL);
	if (!dr)
		return NULL;

	dr->parent = parent;
	dr->start = start;
	dr->n = n;

	res = __request_region(parent, start, n, name, 0);
	if (res)
		devres_add(dev, dr);
	else
		devres_free(dr);

	return res;
}

void __devm_release_region(struct device *dev, struct resource *parent,
			   resource_size_t start, resource_size_t n)
{
	struct region_devres match_data = { parent, start, n };

	__release_region(parent, start, n);
	WARN_ON(devres_destroy(dev, devm_region_release, devm_region_match,
			       &match_data));
}

 
#define MAXRESERVE 4
static int __init reserve_setup(char *str)
{
	static int reserved;
	static struct resource reserve[MAXRESERVE];

	for (;;) {
		unsigned int io_start, io_num;
		int x = reserved;
		struct resource *parent;

		if (get_option(&str, &io_start) != 2)
			break;
		if (get_option(&str, &io_num) == 0)
			break;
		if (x < MAXRESERVE) {
			struct resource *res = reserve + x;

			 
			if (io_start < 0x10000) {
				res->flags = IORESOURCE_IO;
				parent = &ioport_resource;
			} else {
				res->flags = IORESOURCE_MEM;
				parent = &iomem_resource;
			}
			res->name = "reserved";
			res->start = io_start;
			res->end = io_start + io_num - 1;
			res->flags |= IORESOURCE_BUSY;
			res->desc = IORES_DESC_NONE;
			res->child = NULL;
			if (request_resource(parent, res) == 0)
				reserved = x+1;
		}
	}
	return 1;
}
__setup("reserve=", reserve_setup);

 
int iomem_map_sanity_check(resource_size_t addr, unsigned long size)
{
	struct resource *p = &iomem_resource;
	int err = 0;
	loff_t l;

	read_lock(&resource_lock);
	for (p = p->child; p ; p = r_next(NULL, p, &l)) {
		 
		if (p->start >= addr + size)
			continue;
		if (p->end < addr)
			continue;
		if (PFN_DOWN(p->start) <= PFN_DOWN(addr) &&
		    PFN_DOWN(p->end) >= PFN_DOWN(addr + size - 1))
			continue;
		 
		if (p->flags & IORESOURCE_BUSY)
			continue;

		printk(KERN_WARNING "resource sanity check: requesting [mem %#010llx-%#010llx], which spans more than %s %pR\n",
		       (unsigned long long)addr,
		       (unsigned long long)(addr + size - 1),
		       p->name, p);
		err = -1;
		break;
	}
	read_unlock(&resource_lock);

	return err;
}

static int strict_iomem_checks;

 
bool iomem_is_exclusive(u64 addr)
{
	const unsigned int exclusive_system_ram = IORESOURCE_SYSTEM_RAM |
						  IORESOURCE_EXCLUSIVE;
	bool skip_children = false, err = false;
	int size = PAGE_SIZE;
	struct resource *p;

	addr = addr & PAGE_MASK;

	read_lock(&resource_lock);
	for_each_resource(&iomem_resource, p, skip_children) {
		if (p->start >= addr + size)
			break;
		if (p->end < addr) {
			skip_children = true;
			continue;
		}
		skip_children = false;

		 
		if ((p->flags & exclusive_system_ram) == exclusive_system_ram) {
			err = true;
			break;
		}

		 
		if (!strict_iomem_checks || !(p->flags & IORESOURCE_BUSY))
			continue;
		if (IS_ENABLED(CONFIG_IO_STRICT_DEVMEM)
				|| p->flags & IORESOURCE_EXCLUSIVE) {
			err = true;
			break;
		}
	}
	read_unlock(&resource_lock);

	return err;
}

struct resource_entry *resource_list_create_entry(struct resource *res,
						  size_t extra_size)
{
	struct resource_entry *entry;

	entry = kzalloc(sizeof(*entry) + extra_size, GFP_KERNEL);
	if (entry) {
		INIT_LIST_HEAD(&entry->node);
		entry->res = res ? res : &entry->__res;
	}

	return entry;
}

void resource_list_free(struct list_head *head)
{
	struct resource_entry *entry, *tmp;

	list_for_each_entry_safe(entry, tmp, head, node)
		resource_list_destroy_entry(entry);
}


static int __init strict_iomem(char *str)
{
	if (strstr(str, "relaxed"))
		strict_iomem_checks = 0;
	if (strstr(str, "strict"))
		strict_iomem_checks = 1;
	return 1;
}

static int iomem_fs_init_fs_context(struct fs_context *fc)
{
	return init_pseudo(fc, DEVMEM_MAGIC) ? 0 : -ENOMEM;
}

static struct file_system_type iomem_fs_type = {
	.name		= "iomem",
	.owner		= THIS_MODULE,
	.init_fs_context = iomem_fs_init_fs_context,
	.kill_sb	= kill_anon_super,
};

static int __init iomem_init_inode(void)
{
	static struct vfsmount *iomem_vfs_mount;
	static int iomem_fs_cnt;
	struct inode *inode;
	int rc;

	rc = simple_pin_fs(&iomem_fs_type, &iomem_vfs_mount, &iomem_fs_cnt);
	if (rc < 0) {
		pr_err("Cannot mount iomem pseudo filesystem: %d\n", rc);
		return rc;
	}

	inode = alloc_anon_inode(iomem_vfs_mount->mnt_sb);
	if (IS_ERR(inode)) {
		rc = PTR_ERR(inode);
		pr_err("Cannot allocate inode for iomem: %d\n", rc);
		simple_release_fs(&iomem_vfs_mount, &iomem_fs_cnt);
		return rc;
	}

	 
	smp_store_release(&iomem_inode, inode);

	return 0;
}

fs_initcall(iomem_init_inode);

__setup("iomem=", strict_iomem);
