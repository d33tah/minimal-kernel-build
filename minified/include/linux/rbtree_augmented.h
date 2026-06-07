#ifndef _LINUX_RBTREE_AUGMENTED_H
#define _LINUX_RBTREE_AUGMENTED_H

#include <linux/compiler.h>
#include <linux/rbtree.h>

struct rb_augment_callbacks {
	void (*propagate)(struct rb_node *node, struct rb_node *stop);
	void (*copy)(struct rb_node *old, struct rb_node *new);
	void (*rotate)(struct rb_node *old, struct rb_node *new);
};

extern void __rb_insert_augmented(struct rb_node *node, struct rb_root *root,
	void (*augment_rotate)(struct rb_node *old, struct rb_node *new));

static inline void
rb_insert_augmented(struct rb_node *node, struct rb_root *root,
		    const struct rb_augment_callbacks *augment)
{
	__rb_insert_augmented(node, root, augment->rotate);
}

#define RB_DECLARE_CALLBACKS(RBSTATIC, RBNAME,				\
			     RBSTRUCT, RBFIELD, RBAUGMENTED, RBCOMPUTE)	\
static inline void							\
RBNAME ## _propagate(struct rb_node *rb, struct rb_node *stop)		\
{									\
	while (rb != stop) {						\
		RBSTRUCT *node = rb_entry(rb, RBSTRUCT, RBFIELD);	\
		if (RBCOMPUTE(node, true))				\
			break;						\
		rb = rb_parent(&node->RBFIELD);				\
	}								\
}									\
static inline void							\
RBNAME ## _copy(struct rb_node *rb_old, struct rb_node *rb_new)		\
{									\
	RBSTRUCT *old = rb_entry(rb_old, RBSTRUCT, RBFIELD);		\
	RBSTRUCT *new = rb_entry(rb_new, RBSTRUCT, RBFIELD);		\
	new->RBAUGMENTED = old->RBAUGMENTED;				\
}									\
static void								\
RBNAME ## _rotate(struct rb_node *rb_old, struct rb_node *rb_new)	\
{									\
	RBSTRUCT *old = rb_entry(rb_old, RBSTRUCT, RBFIELD);		\
	RBSTRUCT *new = rb_entry(rb_new, RBSTRUCT, RBFIELD);		\
	new->RBAUGMENTED = old->RBAUGMENTED;				\
	RBCOMPUTE(old, false);						\
}									\
RBSTATIC const struct rb_augment_callbacks RBNAME = {			\
	.propagate = RBNAME ## _propagate,				\
	.copy = RBNAME ## _copy,					\
	.rotate = RBNAME ## _rotate					\
};


#define RB_DECLARE_CALLBACKS_MAX(RBSTATIC, RBNAME, RBSTRUCT, RBFIELD,	      \
				 RBTYPE, RBAUGMENTED, RBCOMPUTE)	      \
static inline bool RBNAME ## _compute_max(RBSTRUCT *node, bool exit)	      \
{									      \
	RBSTRUCT *child;						      \
	RBTYPE max = RBCOMPUTE(node);					      \
	if (node->RBFIELD.rb_left) {					      \
		child = rb_entry(node->RBFIELD.rb_left, RBSTRUCT, RBFIELD);   \
		if (child->RBAUGMENTED > max)				      \
			max = child->RBAUGMENTED;			      \
	}								      \
	if (node->RBFIELD.rb_right) {					      \
		child = rb_entry(node->RBFIELD.rb_right, RBSTRUCT, RBFIELD);  \
		if (child->RBAUGMENTED > max)				      \
			max = child->RBAUGMENTED;			      \
	}								      \
	if (exit && node->RBAUGMENTED == max)				      \
		return true;						      \
	node->RBAUGMENTED = max;					      \
	return false;							      \
}									      \
RB_DECLARE_CALLBACKS(RBSTATIC, RBNAME,					      \
		     RBSTRUCT, RBFIELD, RBAUGMENTED, RBNAME ## _compute_max)

#endif
