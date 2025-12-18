#ifndef _LINUX_MEMORY_H_
#define _LINUX_MEMORY_H_

#include <linux/node.h>
#include <linux/compiler.h>
#include <linux/mutex.h>

/* Unused memory hotplug structs removed: memory_group, memory_block, memory_notify */
/* Unused macros removed: MIN_MEMORY_BLOCK_SIZE, MEM_ONLINE, MEM_OFFLINE, etc. */
/* Unused functions removed: arch_get_memory_phys_device, memory_block_size_bytes, etc. */
/* Unused notifier stubs removed: register_memory_notifier, memory_notify, etc. */

static inline void memory_dev_init(void) { }
#define register_hotmemory_notifier(nb)    ({ (void)(nb); 0; })
#define unregister_hotmemory_notifier(nb)  ({ (void)(nb); })

extern struct mutex text_mutex;

#endif
