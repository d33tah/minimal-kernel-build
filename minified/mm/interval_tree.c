/* Stubbed - interval trees not needed for hello-world (no page reclaim/swap) */
#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/rmap.h>

void vma_interval_tree_insert(struct vm_area_struct *node,
			      struct rb_root_cached *root)
{
}

void vma_interval_tree_remove(struct vm_area_struct *node,
			      struct rb_root_cached *root)
{
}

void anon_vma_interval_tree_insert(struct anon_vma_chain *node,
				   struct rb_root_cached *root)
{
}

void anon_vma_interval_tree_remove(struct anon_vma_chain *node,
				   struct rb_root_cached *root)
{
}
