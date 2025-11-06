// SPDX-License-Identifier: GPL-2.0-only
/* Stubbed notifier.c */
#include <linux/notifier.h>
#include <linux/export.h>
#include <linux/kdebug.h>

BLOCKING_NOTIFIER_HEAD(reboot_notifier_list);

int atomic_notifier_chain_register(struct atomic_notifier_head *nh,
				   struct notifier_block *n) { return 0; }
EXPORT_SYMBOL_GPL(atomic_notifier_chain_register);

int atomic_notifier_chain_register_unique_prio(struct atomic_notifier_head *nh,
					       struct notifier_block *n) { return 0; }
EXPORT_SYMBOL_GPL(atomic_notifier_chain_register_unique_prio);

int atomic_notifier_chain_unregister(struct atomic_notifier_head *nh,
				     struct notifier_block *n) { return 0; }
EXPORT_SYMBOL_GPL(atomic_notifier_chain_unregister);

int atomic_notifier_call_chain(struct atomic_notifier_head *nh,
			       unsigned long val, void *v) { return 0; }
EXPORT_SYMBOL_GPL(atomic_notifier_call_chain);

int blocking_notifier_chain_register(struct blocking_notifier_head *nh,
				     struct notifier_block *n) { return 0; }
EXPORT_SYMBOL_GPL(blocking_notifier_chain_register);

int blocking_notifier_chain_register_unique_prio(struct blocking_notifier_head *nh,
						 struct notifier_block *n) { return 0; }
EXPORT_SYMBOL_GPL(blocking_notifier_chain_register_unique_prio);

int blocking_notifier_chain_unregister(struct blocking_notifier_head *nh,
				       struct notifier_block *n) { return 0; }
EXPORT_SYMBOL_GPL(blocking_notifier_chain_unregister);

int blocking_notifier_call_chain_robust(struct blocking_notifier_head *nh,
					unsigned long val_up, unsigned long val_down, void *v) { return 0; }
EXPORT_SYMBOL_GPL(blocking_notifier_call_chain_robust);

int blocking_notifier_call_chain(struct blocking_notifier_head *nh,
				 unsigned long val, void *v) { return 0; }
EXPORT_SYMBOL_GPL(blocking_notifier_call_chain);

int raw_notifier_chain_register(struct raw_notifier_head *nh,
				struct notifier_block *n) { return 0; }
EXPORT_SYMBOL_GPL(raw_notifier_chain_register);

int raw_notifier_chain_unregister(struct raw_notifier_head *nh,
				  struct notifier_block *n) { return 0; }
EXPORT_SYMBOL_GPL(raw_notifier_chain_unregister);

int raw_notifier_call_chain_robust(struct raw_notifier_head *nh,
				   unsigned long val_up, unsigned long val_down, void *v) { return 0; }
EXPORT_SYMBOL_GPL(raw_notifier_call_chain_robust);

int raw_notifier_call_chain(struct raw_notifier_head *nh,
			    unsigned long val, void *v) { return 0; }
EXPORT_SYMBOL_GPL(raw_notifier_call_chain);

int srcu_notifier_chain_register(struct srcu_notifier_head *nh,
				 struct notifier_block *n) { return 0; }
EXPORT_SYMBOL_GPL(srcu_notifier_chain_register);

int srcu_notifier_chain_unregister(struct srcu_notifier_head *nh,
				   struct notifier_block *n) { return 0; }
EXPORT_SYMBOL_GPL(srcu_notifier_chain_unregister);

int srcu_notifier_call_chain(struct srcu_notifier_head *nh,
			     unsigned long val, void *v) { return 0; }
EXPORT_SYMBOL_GPL(srcu_notifier_call_chain);

void srcu_init_notifier_head(struct srcu_notifier_head *nh) { }
EXPORT_SYMBOL_GPL(srcu_init_notifier_head);

int register_die_notifier(struct notifier_block *nb) { return 0; }
EXPORT_SYMBOL_GPL(register_die_notifier);

int unregister_die_notifier(struct notifier_block *nb) { return 0; }
EXPORT_SYMBOL_GPL(unregister_die_notifier);

int notify_die(enum die_val val, const char *str, struct pt_regs *regs,
	       long err, int trap, int sig) { return 0; }

bool atomic_notifier_call_chain_is_empty(struct atomic_notifier_head *nh) { return true; }
