/* SPDX-License-Identifier: GPL-2.0 */
#ifndef LINUX_CRASH_DUMP_H
#define LINUX_CRASH_DUMP_H

#include <linux/kexec.h>
#include <linux/proc_fs.h>
#include <linux/elf.h>
#include <linux/pgtable.h>
#include <uapi/linux/vmcore.h>

/* For IS_ENABLED(CONFIG_CRASH_DUMP) */
#define ELFCORE_ADDR_MAX	(-1ULL)
#define ELFCORE_ADDR_ERR	(-2ULL)

extern unsigned long long elfcorehdr_addr;
extern unsigned long long elfcorehdr_size;

static inline bool is_kdump_kernel(void) { return false; }

/* Device Dump information to be filled by drivers */
struct vmcoredd_data {
	char dump_name[VMCOREDD_MAX_NAME_BYTES]; /* Unique name of the dump */
	unsigned int size;                       /* Size of the dump */
	/* Driver's registered callback to be invoked to collect dump */
	int (*vmcoredd_callback)(struct vmcoredd_data *data, void *buf);
};

static inline int vmcore_add_device_dump(struct vmcoredd_data *data)
{
	return -EOPNOTSUPP;
}

static inline ssize_t read_from_oldmem(struct iov_iter *iter, size_t count,
				       u64 *ppos, bool encrypted)
{
	return -EOPNOTSUPP;
}

#endif /* LINUX_CRASHDUMP_H */
