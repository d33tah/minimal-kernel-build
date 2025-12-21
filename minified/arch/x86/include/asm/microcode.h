
#ifndef _ASM_X86_MICROCODE_H
#define _ASM_X86_MICROCODE_H

#include <asm/cpu.h>
#include <linux/initrd.h>

#define MAX_CPIO_FILE_NAME 18
struct cpio_data {
	void *data;
	size_t size;
	char name[MAX_CPIO_FILE_NAME];
};
struct cpio_data find_cpio_data(const char *path, void *data, size_t len, long *offset);

struct ucode_patch {
	struct list_head plist;
	void *data;		 
	u32 patch_id;
	u16 equiv_cpu;
};

extern struct list_head microcode_cache;

struct cpu_signature {
	unsigned int sig;
	unsigned int pf;
	unsigned int rev;
};

struct device;

enum ucode_state {
	UCODE_OK	= 0,
	UCODE_NEW,
	UCODE_UPDATED,
	UCODE_NFOUND,
	UCODE_ERROR,
};

struct microcode_ops {
	enum ucode_state (*request_microcode_user) (int cpu,
				const void __user *buf, size_t size);

	enum ucode_state (*request_microcode_fw) (int cpu, struct device *,
						  bool refresh_fw);

	void (*microcode_fini_cpu) (int cpu);

	 
	enum ucode_state (*apply_microcode) (int cpu);
	int (*collect_cpu_info) (int cpu, struct cpu_signature *csig);
};

struct ucode_cpu_info {
	struct cpu_signature	cpu_sig;
	int			valid;
	void			*mc;
};
extern struct ucode_cpu_info ucode_cpu_info[];
struct cpio_data find_microcode_in_initrd(const char *path, bool use_pa);

/* init_intel_microcode, init_amd_microcode, exit_amd_microcode, x86_cpuid_vendor,
   x86_cpuid_family, load_ucode_bsp, load_ucode_ap, reload_early_microcode,
   microcode_bsp_resume removed - unused */

#endif
