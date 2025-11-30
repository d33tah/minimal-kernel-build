/* Stub: CPU topology registration not needed for minimal kernel */
#include <linux/kernel.h>
#include <linux/init.h>

static int __init topology_init(void) { return 0; }
subsys_initcall(topology_init);
