/* Stub SMP boot - single CPU only */
#include <linux/smpboot.h>
#include <linux/stddef.h>

int smpboot_register_percpu_thread(struct smp_hotplug_thread *plug_thread)
{
	return 0;
}
