
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

struct cpu_signature {
	unsigned int sig;
	unsigned int pf;
	unsigned int rev;
};

struct device;

struct ucode_cpu_info {
	struct cpu_signature	cpu_sig;
	int			valid;
	void			*mc;
};
extern struct ucode_cpu_info ucode_cpu_info[];
struct cpio_data find_microcode_in_initrd(const char *path, bool use_pa);

#define MAX_UCODE_COUNT 128

#define QCHAR(a, b, c, d) ((a) + ((b) << 8) + ((c) << 16) + ((d) << 24))
#define CPUID_INTEL1 QCHAR('G', 'e', 'n', 'u')
#define CPUID_INTEL2 QCHAR('i', 'n', 'e', 'I')
#define CPUID_INTEL3 QCHAR('n', 't', 'e', 'l')
#define CPUID_AMD1 QCHAR('A', 'u', 't', 'h')
#define CPUID_AMD2 QCHAR('e', 'n', 't', 'i')
#define CPUID_AMD3 QCHAR('c', 'A', 'M', 'D')

#define CPUID_IS(a, b, c, ebx, ecx, edx)	\
		(!((ebx ^ (a))|(edx ^ (b))|(ecx ^ (c))))

static inline void __init load_ucode_bsp(void)			{ }
static inline void load_ucode_ap(void)				{ }

#endif  
