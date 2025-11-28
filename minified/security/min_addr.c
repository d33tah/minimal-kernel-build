/* Stub: mmap_min_addr handling simplified for minimal kernel */
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/security.h>
#include <linux/sysctl.h>

unsigned long mmap_min_addr = CONFIG_DEFAULT_MMAP_MIN_ADDR;
unsigned long dac_mmap_min_addr = CONFIG_DEFAULT_MMAP_MIN_ADDR;

int mmap_min_addr_handler(struct ctl_table *table, int write,
			  void *buffer, size_t *lenp, loff_t *ppos) { return 0; }

static int __init init_mmap_min_addr(void) { return 0; }
pure_initcall(init_mmap_min_addr);
