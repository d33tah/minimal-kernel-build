
#ifndef BOOT_VIDEO_H
#define BOOT_VIDEO_H

#include <linux/types.h>

struct card_info {
	const char *card_name;
};

#define __videocard struct card_info __section(".videocards") __attribute__((used))
extern struct card_info video_cards[], video_cards_end[];

#endif
