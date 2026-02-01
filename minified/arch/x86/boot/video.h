 
 

 

#ifndef BOOT_VIDEO_H
#define BOOT_VIDEO_H

#include <linux/types.h>

#define VIDEO_FIRST_BIOS 0x0100
#define VIDEO_FIRST_VESA 0x0200
#define VIDEO_80x25 0x0f00
#define VIDEO_CURRENT_MODE 0x0f04
#define VIDEO_RECALC 0x8000

struct mode_info {
	u16 mode;		 
	u16 x, y;		 
	u16 depth;		 
};

struct card_info {
	const char *card_name;
	int (*set_mode)(struct mode_info *mode);
	int (*probe)(void);
	struct mode_info *modes;
	int nmodes;		 
	int unsafe;		 
	u16 xmode_first;	 
	u16 xmode_n;		 
};

#define __videocard struct card_info __section(".videocards") __attribute__((used))
extern struct card_info video_cards[], video_cards_end[];

#define ADAPTER_CGA	0	 
#define ADAPTER_EGA	1
#define ADAPTER_VGA	2

extern int adapter;
extern int force_x, force_y;
/* do_restore removed - never read */
extern int graphic_mode;

/* in_idx, out_idx removed - never called */

#endif
