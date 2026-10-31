
#include <linux/build_bug.h>

#include "boot.h"
#include "string.h"

struct boot_params boot_params __attribute__((aligned(16)));

struct port_io_ops pio_ops;

static void copy_boot_params(void)
{
	BUILD_BUG_ON(sizeof(boot_params) != 4096);
	memcpy(&boot_params.hdr, &hdr, sizeof(hdr));
}

void main(void)
{
	init_default_io_ops();

	copy_boot_params();

	detect_memory();

	set_video();

	go_to_protected_mode();
}
