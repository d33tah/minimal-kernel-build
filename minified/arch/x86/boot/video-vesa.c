/* Minimal VESA stub - no VESA modes needed for text-only boot */
#include "boot.h"
#include "video.h"

/*
 * The VESA __videocard entry and its vesa_probe()/vesa_set_mode() handlers
 * were dead in this minimal 80x25-text boot: vesa_probe() reported 0 modes,
 * so raw_set_mode() never selected the VESA card (video-vga wins).  Removing
 * the .videocards struct + both handlers makes probe_cards() iterate one
 * fewer no-op card; the text mode setup is unchanged.  vesa_store_edid()
 * stays: it is still called unconditionally from set_video() (video.c).
 */

#ifndef _WAKEUP
void vesa_store_edid(void)
{
}
#endif
