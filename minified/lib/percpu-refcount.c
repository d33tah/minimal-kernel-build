// SPDX-License-Identifier: GPL-2.0-only
// Stubbed - no external usage found
#include <linux/kernel.h>
#include <linux/percpu-refcount.h>

int percpu_ref_init(struct percpu_ref *ref, percpu_ref_func_t *release,
		    unsigned int flags, gfp_t gfp) { return -EINVAL; }
EXPORT_SYMBOL_GPL(percpu_ref_init);

void percpu_ref_exit(struct percpu_ref *ref) { }
EXPORT_SYMBOL_GPL(percpu_ref_exit);

void percpu_ref_switch_to_atomic(struct percpu_ref *ref,
				 percpu_ref_func_t *confirm_switch) { }
EXPORT_SYMBOL_GPL(percpu_ref_switch_to_atomic);

void percpu_ref_switch_to_atomic_sync(struct percpu_ref *ref) { }
EXPORT_SYMBOL_GPL(percpu_ref_switch_to_atomic_sync);

void percpu_ref_switch_to_percpu(struct percpu_ref *ref) { }
EXPORT_SYMBOL_GPL(percpu_ref_switch_to_percpu);

void percpu_ref_kill_and_confirm(struct percpu_ref *ref,
				 percpu_ref_func_t *confirm_kill) { }
EXPORT_SYMBOL_GPL(percpu_ref_kill_and_confirm);

bool percpu_ref_is_zero(struct percpu_ref *ref) { return false; }
EXPORT_SYMBOL_GPL(percpu_ref_is_zero);

void percpu_ref_reinit(struct percpu_ref *ref) { }
EXPORT_SYMBOL_GPL(percpu_ref_reinit);

void percpu_ref_resurrect(struct percpu_ref *ref) { }
EXPORT_SYMBOL_GPL(percpu_ref_resurrect);
