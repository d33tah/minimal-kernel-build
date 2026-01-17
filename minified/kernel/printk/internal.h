
#include <linux/percpu.h>
/* printk_sysctl_init removed - never called */
#define printk_safe_enter_irqsave(flags) local_irq_save(flags)
#define printk_safe_exit_irqrestore(flags) local_irq_restore(flags)
/* Removed uncalled: printk_percpu_data_ready */
