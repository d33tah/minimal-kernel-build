
#include <linux/linkage.h>
#include <linux/errno.h>
#include <linux/syscalls.h>

#include <asm/syscall_wrapper.h>

/* All posix timer/clock SYS_NI aliases and SYSCALL_DEFINE stubs removed
   - none of these are in the reduced syscall table */
