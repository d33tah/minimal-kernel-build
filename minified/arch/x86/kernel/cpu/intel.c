
#include <linux/kernel.h>

#include <asm/cpu.h>
#include <asm/bugs.h>
#include <asm/microcode_intel.h>

int intel_cpu_collect_info(struct ucode_cpu_info *uci)
{
	return -1;
}

int ppro_with_ram_bug(void)
{
	return 0;
}

void handle_bus_lock(struct pt_regs *regs)
{
}

void __init sld_setup(struct cpuinfo_x86 *c)
{
}

bool handle_user_split_lock(struct pt_regs *regs, long error_code)
{
	return false;
}
