
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/export.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/fs.h>
#include <linux/fs_context.h>
#include <linux/proc_fs.h>
#include <linux/sched.h>
#include <linux/seq_file.h>
#include <linux/device.h>
#include <linux/pfn.h>
#include <linux/mm.h>
#include <linux/mount.h>
#include <linux/acpi.h>
#include <uapi/linux/magic.h>
#include <asm/io.h>

struct resource ioport_resource = {
	.name = "PCI IO",
	.start = 0,
	.end = IO_SPACE_LIMIT,
	.flags = IORESOURCE_IO,
};

struct resource iomem_resource = {
	.name = "PCI mem",
	.start = 0,
	.end = -1,
	.flags = IORESOURCE_MEM,
};

/* struct resource_constraint removed - never used */

static DEFINE_RWLOCK(resource_lock);

static struct resource *next_resource(struct resource *p)
{
	if (p->child)
		return p->child;
	while (!p->sibling && p->parent)
		p = p->parent;
	return p->sibling;
}

/* for_each_resource removed - never used */

static void free_resource(struct resource *res)
{
	if (res && PageSlab(virt_to_head_page(res)))
		kfree(res);
}

static struct resource *__request_resource(struct resource *root,
					   struct resource *new)
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

/* __release_resource removed - no callers (~28 LOC) */

struct resource *request_resource_conflict(struct resource *root,
					   struct resource *new)
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
/* release_resource removed - never called (~9 LOC) */

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
		*res = (struct resource){
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

int walk_mem_res(u64 start, u64 end, void *arg,
		 int (*func)(struct resource *, void *))
{
	unsigned long flags = IORESOURCE_MEM | IORESOURCE_BUSY;
	return __walk_iomem_res_desc(start, end, flags, IORES_DESC_NONE, arg,
				     func);
}

/* STUB: unused resource allocation/lookup functions */
static struct resource *__insert_resource(struct resource *parent,
					  struct resource *new)
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

	for (next = first;; next = next->sibling) {
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

struct resource *insert_resource_conflict(struct resource *parent,
					  struct resource *new)
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

static DECLARE_WAIT_QUEUE_HEAD(muxed_resource_wait);

static int __request_region_locked(struct resource *res,
				   struct resource *parent,
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
	/* Inlined alloc_resource */
	struct resource *res = kzalloc(sizeof(struct resource), GFP_KERNEL);
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

	/* revoke_iomem removed - empty stub */

	return res;
}

/* __release_region, MAXRESERVE removed - never used */
/* iomem_map_sanity_check removed - caller site simplified */
/* iomem_fs_type and iomem_fs_init_fs_context removed - never registered */
/* iomem_init_inode removed - simple_pin_fs hangs with low memory */
