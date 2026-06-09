/* Minimal sys.c - overflow uid/gid for !setuid-syscall kernels */
#include <linux/highuid.h>

int overflowuid = DEFAULT_OVERFLOWUID;
int overflowgid = DEFAULT_OVERFLOWGID;
