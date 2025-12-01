/* Minimal BIOS video stub - no BIOS extended modes needed */
#include "boot.h"
#include "video.h"

static __videocard video_bios;

static int bios_set_mode(struct mode_info *mi)
{
	return -1;  /* BIOS extended modes not supported */
}

static int bios_probe(void)
{
	return 0;  /* No BIOS modes for minimal kernel */
}

static __videocard video_bios =
{
	.card_name	= "BIOS",
	.probe		= bios_probe,
	.set_mode	= bios_set_mode,
	.unsafe		= 1,
	.xmode_first	= VIDEO_FIRST_BIOS,
	.xmode_n	= 0x80,
};
