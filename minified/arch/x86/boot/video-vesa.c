/* Minimal VESA stub - no VESA modes needed for text-only boot */
#include "boot.h"
#include "video.h"
#include "vesa.h"

static __videocard video_vesa;

/* VESA probe - return 0 modes (no VESA graphics for minimal kernel) */
static int vesa_probe(void)
{
	return 0;
}

static int vesa_set_mode(struct mode_info *mode)
{
	return -1;  /* VESA modes not supported */
}

#ifndef _WAKEUP
void vesa_store_edid(void)
{
}
#endif

static __videocard video_vesa =
{
	.card_name	= "VESA",
	.probe		= vesa_probe,
	.set_mode	= vesa_set_mode,
	.xmode_first	= VIDEO_FIRST_VESA,
	.xmode_n	= 0x200,
};
