
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/fs.h>
#include <linux/fs_context.h>
/* proc_fs.h removed - empty header */
#include <linux/sched.h>
#include <linux/device.h>
#include <linux/pfn.h>
#include <linux/mm.h>
#include <linux/mount.h>
/* magic.h removed - not used */
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

static DEFINE_RWLOCK(resource_lock);

/* next_resource inlined into find_next_iomem_res loop */

/* free_resource inlined into single caller */

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

/* __release_resource, request_resource_conflict, release_resource removed */

int request_resource(struct resource *root, struct resource *new)
{
	struct resource *conflict;

	write_lock(&resource_lock);
	conflict = __request_resource(root, new);
	write_unlock(&resource_lock);
	return conflict ? -EBUSY : 0;
}

/* find_next_iomem_res inlined into walk_mem_res */

int walk_mem_res(u64 start, u64 end, void *arg,
		 int (*func)(struct resource *, void *))
{
	unsigned long flags = IORESOURCE_MEM | IORESOURCE_BUSY;
	struct resource res;
	struct resource *p;
	int ret = -EINVAL;

	while (start < end) {
		read_lock(&resource_lock);
		for (p = iomem_resource.child; p;
		     p = (p->child) ? p->child : (({
			     struct resource *__p = p;
			     while (!__p->sibling && __p->parent)
				     __p = __p->parent;
			     __p->sibling;
		     }))) {
			if (p->start > end) {
				p = NULL;
				break;
			}
			if (p->end < start)
				continue;
			if ((p->flags & flags) != flags)
				continue;
			break;
		}
		if (p) {
			res = (struct resource){
				.start = max(start, p->start),
				.end = min(end, p->end),
				.flags = p->flags,
				.desc = p->desc,
				.parent = p->parent,
			};
		}
		read_unlock(&resource_lock);
		if (!p)
			break;
		ret = (*func)(&res, arg);
		if (ret)
			break;
		start = res.end + 1;
	}
	return ret;
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

/* insert_resource_conflict inlined into insert_resource - single caller */
int insert_resource(struct resource *parent, struct resource *new)
{
	struct resource *conflict;

	write_lock(&resource_lock);
	conflict = __insert_resource(parent, new);
	write_unlock(&resource_lock);
	return conflict ? -EBUSY : 0;
}

/* __request_region removed - never called (~50 LOC) */

/* iomem_init_inode removed - simple_pin_fs hangs with low memory */
