
#ifndef _LINUX_IRQDOMAIN_H
#define _LINUX_IRQDOMAIN_H

#include <linux/types.h>
#include <linux/of.h>
#include <linux/mutex.h>
#include <linux/radix-tree.h>

/*
 * struct irq_domain and its entire revmap/ops API were never instantiated or
 * read in this build (no irq_find_mapping/irq_domain_associate/__irq_domain_add
 * callers, struct irq_data only holds an undereferenced irq_domain* pointer).
 * The definitions were removed; only the transitive includes above are kept.
 */

#endif
