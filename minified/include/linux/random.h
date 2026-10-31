
#ifndef _LINUX_RANDOM_H
#define _LINUX_RANDOM_H

#include <linux/bug.h>
#include <linux/kernel.h>
#include <linux/list.h>

void get_random_bytes(void *buf, size_t len);

#endif
