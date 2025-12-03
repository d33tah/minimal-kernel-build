#ifndef __LINUX_KEYBOARD_H
#define __LINUX_KEYBOARD_H

#include <uapi/linux/keyboard.h>

extern unsigned short *key_maps[MAX_NR_KEYMAPS];
extern unsigned short plain_map[NR_KEYS];

/* keyboard_notifier_param struct, register/unregister_keyboard_notifier removed - unused */
#endif
