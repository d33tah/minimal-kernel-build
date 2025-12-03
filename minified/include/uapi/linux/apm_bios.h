#ifndef _UAPI_LINUX_APM_H
#define _UAPI_LINUX_APM_H

/* Minimal apm_bios.h - only struct needed by bootparam.h */

#include <linux/types.h>

struct apm_bios_info {
	__u16	version;
	__u16	cseg;
	__u32	offset;
	__u16	cseg_16;
	__u16	dseg;
	__u16	flags;
	__u16	cseg_len;
	__u16	cseg_16_len;
	__u16	dseg_len;
};

#endif  
