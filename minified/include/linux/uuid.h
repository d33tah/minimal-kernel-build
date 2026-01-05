#ifndef _LINUX_UUID_H_
#define _LINUX_UUID_H_

#include <linux/types.h>

typedef struct {
	__u8 b[16];
} guid_t;

#endif
