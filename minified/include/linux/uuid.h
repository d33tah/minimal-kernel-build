#ifndef _LINUX_UUID_H_
#define _LINUX_UUID_H_

#include <linux/types.h>
#include <linux/string.h>

typedef struct {
	__u8 b[16];
} guid_t;

#define UUID_SIZE 16

typedef struct {
	__u8 b[UUID_SIZE];
} uuid_t;

#endif
