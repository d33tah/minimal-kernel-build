#include "boot.h"
#include "video.h"
#include <uapi/asm/boot.h>

int adapter;
int force_x, force_y;

void probe_cards(int unsafe)
{
}

int set_mode(u16 mode)
{
	return -1;
}
