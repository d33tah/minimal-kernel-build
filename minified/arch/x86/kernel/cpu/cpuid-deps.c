#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <asm/cpufeature.h>

struct cpuid_dep {
	unsigned int	feature;
	unsigned int	depends;
};

/* Minimal dependencies for basic boot - advanced features removed */
static const struct cpuid_dep cpuid_deps[] = {
	{ X86_FEATURE_FXSR,			X86_FEATURE_FPU	      },
	{ X86_FEATURE_XSAVE,			X86_FEATURE_FXSR      },
	{ X86_FEATURE_XMM,			X86_FEATURE_FXSR      },
	{ X86_FEATURE_XMM2,			X86_FEATURE_XMM       },
	{}
};

static inline void clear_feature(struct cpuinfo_x86 *c, unsigned int feature)
{
	 
	if (!c) {
		clear_cpu_cap(&boot_cpu_data, feature);
		set_bit(feature, (unsigned long *)cpu_caps_cleared);
	} else {
		clear_bit(feature, (unsigned long *)c->x86_capability);
	}
}

#define MAX_FEATURE_BITS ((NCAPINTS + NBUGINTS) * sizeof(u32) * 8)

static void do_clear_cpu_cap(struct cpuinfo_x86 *c, unsigned int feature)
{
	DECLARE_BITMAP(disable, MAX_FEATURE_BITS);
	const struct cpuid_dep *d;
	bool changed;

	if (WARN_ON(feature >= MAX_FEATURE_BITS))
		return;

	clear_feature(c, feature);

	 
	memset(disable, 0, sizeof(disable));
	__set_bit(feature, disable);

	 
	do {
		changed = false;
		for (d = cpuid_deps; d->feature; d++) {
			if (!test_bit(d->depends, disable))
				continue;
			if (__test_and_set_bit(d->feature, disable))
				continue;

			changed = true;
			clear_feature(c, d->feature);
		}
	} while (changed);
}

void clear_cpu_cap(struct cpuinfo_x86 *c, unsigned int feature)
{
	do_clear_cpu_cap(c, feature);
}

void setup_clear_cpu_cap(unsigned int feature)
{
	do_clear_cpu_cap(NULL, feature);
}
