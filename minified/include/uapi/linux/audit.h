/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/* Minimal audit.h - only contains definitions needed for minimal kernel */

#ifndef _UAPI_LINUX_AUDIT_H_
#define _UAPI_LINUX_AUDIT_H_

#include <linux/types.h>
#include <linux/elf-em.h>

/* Architecture flags */
#define __AUDIT_ARCH_64BIT 0x80000000
#define __AUDIT_ARCH_LE	   0x40000000

/* Architecture definitions - only keep what we need */
#define AUDIT_ARCH_I386		(EM_386|__AUDIT_ARCH_LE)

#endif /* _UAPI_LINUX_AUDIT_H_ */
