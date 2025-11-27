 
 

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

#define for_each_resource(_root, _p, _skip_children) \
	for ((_p) = (_root)->child; (_p); \
	     (_p) = next_resource(_p))


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

/* Stub: release_child_resources not used in minimal kernel */
void release_child_resources(struct resource *r)
{
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

/* Stubbed - not used externally */
int walk_iomem_res_desc(unsigned long desc, unsigned long flags, u64 start,
		u64 end, void *arg, int (*func)(struct resource *, void *))
{
	return -EINVAL;
}

int walk_system_ram_res(u64 start, u64 end, void *arg,
			int (*func)(struct resource *, void *))
{
	return -EINVAL;
}

 
int walk_mem_res(u64 start, u64 end, void *arg,
		 int (*func)(struct resource *, void *))
{
	unsigned long flags = IORESOURCE_MEM | IORESOURCE_BUSY;

	return __walk_iomem_res_desc(start, end, flags, IORES_DESC_NONE, arg,
				     func);
}

/* Stubbed - not used externally */
int walk_system_ram_range(unsigned long start_pfn, unsigned long nr_pages,
			  void *arg, int (*func)(unsigned long, unsigned long, void *))
{
	return -EINVAL;
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

/* STUB: unused resource allocation/lookup functions */
int allocate_resource(struct resource *root, struct resource *new,
		      resource_size_t size, resource_size_t min,
		      resource_size_t max, resource_size_t align,
		      resource_size_t (*alignf)(void *,
						const struct resource *,
						resource_size_t,
						resource_size_t),
		      void *alignf_data)
{ return -ENOMEM; }

struct resource *lookup_resource(struct resource *root, resource_size_t start)
{ return NULL; }

 
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

/* Stub: insert_resource_expand_to_fit not used in minimal kernel */
void insert_resource_expand_to_fit(struct resource *root, struct resource *new)
{ }

/* STUB: unused remove/adjust resource functions */
int remove_resource(struct resource *old) { return -EINVAL; }
int adjust_resource(struct resource *res, resource_size_t start,
		    resource_size_t size) { return -EBUSY; }

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

 
/* Stub: resource_alignment not used in minimal kernel */
resource_size_t resource_alignment(struct resource *res)
{
	return 0;
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



/* Stubbed: devm_request_resource not used externally */
int devm_request_resource(struct device *dev, struct resource *root,
			  struct resource *new) { return -ENOSYS; }

/* Stubbed: devm_release_resource not used externally */
void devm_release_resource(struct device *dev, struct resource *new) { }

/* Stubbed: __devm_request_region not used externally */
struct resource *
__devm_request_region(struct device *dev, struct resource *parent,
		      resource_size_t start, resource_size_t n, const char *name)
{ return NULL; }

/* Stubbed: __devm_release_region not used externally */
void __devm_release_region(struct device *dev, struct resource *parent,
			   resource_size_t start, resource_size_t n) { }

 
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
	/* Stub: sanity check not needed for minimal kernel */
	return 0;
}

static int strict_iomem_checks;

bool iomem_is_exclusive(u64 addr)
{
	/* Stub: exclusivity check not needed for minimal kernel */
	return false;
}

/* Stub: resource_list_create_entry not used externally */
struct resource_entry *resource_list_create_entry(struct resource *res,
						  size_t extra_size)
{
	return NULL;
}

/* Stub: resource_list_free not used externally */
void resource_list_free(struct list_head *head)
{
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
