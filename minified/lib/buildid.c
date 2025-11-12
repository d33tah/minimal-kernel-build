// SPDX-License-Identifier: GPL-2.0
/* Build ID parsing - STUBBED
 * Build ID is used for debug/profiling, not needed for minimal boot
 */

#include <linux/buildid.h>
#include <linux/cache.h>
#include <linux/elf.h>
#include <linux/kernel.h>
#include <linux/pagemap.h>

#define BUILD_ID 3

/* Stub - always fail to parse build ID */
int build_id_parse(struct vm_area_struct *vma, unsigned char *build_id, __u32 *size)
{
	return -EINVAL;
}

/* Stub - always fail to parse build ID from buffer */
int build_id_parse_buf(const void *buf, unsigned char *build_id, u32 buf_size)
{
	return -EINVAL;
}

#if IS_ENABLED(CONFIG_STACKTRACE_BUILD_ID) || IS_ENABLED(CONFIG_CRASH_CORE)
unsigned char vmlinux_build_id[BUILD_ID_SIZE_MAX] __ro_after_init;

/* Stub - do nothing, leave build ID zeroed */
void __init init_vmlinux_build_id(void)
{
	/* No-op: build ID not needed for minimal boot */
}
#endif
