 
 
#include <linux/percpu.h>

#define printk_sysctl_init() do { } while (0)


 
#define printk_safe_enter_irqsave(flags) local_irq_save(flags)
#define printk_safe_exit_irqrestore(flags) local_irq_restore(flags)

static inline bool printk_percpu_data_ready(void) { return false; }
