/* Minimal syscall stubs - most removed after reducing syscall table to 5 entries */
#include <linux/syscalls.h>
#include <linux/utsname.h>
#include <linux/fs.h>

int overflowuid = DEFAULT_OVERFLOWUID;
int overflowgid = DEFAULT_OVERFLOWGID;

/* All 29 SYSCALL_DEFINE stubs removed - no longer in syscall table */
