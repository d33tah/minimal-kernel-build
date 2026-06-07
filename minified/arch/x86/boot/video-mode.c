/* Minimal video mode setup - simplified for text-only boot */
#include "boot.h"
#include "video.h"
#include "vesa.h"
#include <uapi/asm/boot.h>

int adapter;
int force_x, force_y;
int do_restore;
int graphic_mode;

void probe_cards(int unsafe)
{
	struct card_info *card;
	static u8 probed[2];

	if (probed[unsafe])
		return;

	probed[unsafe] = 1;

	for (card = video_cards; card < video_cards_end; card++) {
		if (card->unsafe == unsafe) {
			if (card->probe)
				card->nmodes = card->probe();
			else
				card->nmodes = 0;
		}
	}
}

int mode_defined(u16 mode)
{
	struct card_info *card;
	struct mode_info *mi;
	int i;

	for (card = video_cards; card < video_cards_end; card++) {
		mi = card->modes;
		for (i = 0; i < card->nmodes; i++, mi++) {
			if (mi->mode == mode)
				return 1;
		}
	}
	return 0;
}

static int raw_set_mode(u16 mode, u16 *real_mode)
{
	int nmode, i;
	struct card_info *card;
	struct mode_info *mi;

	mode &= ~VIDEO_RECALC;

	nmode = 0;
	for (card = video_cards; card < video_cards_end; card++) {
		mi = card->modes;
		for (i = 0; i < card->nmodes; i++, mi++) {
			int visible = mi->x || mi->y;

			if ((mode == nmode && visible) ||
			    mode == mi->mode ||
			    mode == (mi->y << 8)+mi->x) {
				*real_mode = mi->mode;
				return card->set_mode(mi);
			}

			if (visible)
				nmode++;
		}
	}

	return -1;
}

int set_mode(u16 mode)
{
	int rv;
	u16 real_mode;

	if (mode == VIDEO_CURRENT_MODE)
		return 0;
	else if (mode == NORMAL_VGA)
		mode = VIDEO_80x25;
	else if (mode == EXTENDED_VGA)
		mode = VIDEO_80x25;  /* Use 80x25 instead of 8POINT */

	rv = raw_set_mode(mode, &real_mode);
	if (rv)
		return rv;

#ifndef _WAKEUP
	boot_params.hdr.vid_mode = real_mode;
#endif
	return 0;
}
