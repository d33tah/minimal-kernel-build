
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/errno.h>
#include <linux/ioport.h>
/* linux/slab.h removed - no slab functions */
#include <linux/spinlock.h>
/* linux/fs.h removed - no fs types used */
/* linux/fs_context.h removed - no fs_context types used */
/* proc_fs.h removed - empty header */
#include <linux/sched.h>
/* linux/device.h removed - no device types used */
#include <linux/pfn.h>
#include <linux/mm.h>
/* linux/mount.h removed - no mount types used */
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

/* walk_mem_res removed - only caller (ioremap) was removed */

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
