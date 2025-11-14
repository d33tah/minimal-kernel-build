// SPDX-License-Identifier: GPL-2.0
/* Minimal keyboard map stubs - keyboard input not needed */

#include <linux/types.h>
#include <linux/keyboard.h>
#include <linux/kd.h>

/* Minimal plain map - just first few keys */
unsigned short plain_map[NR_KEYS] = { [0 ... (NR_KEYS-1)] = 0 };

/* Minimal keymaps - only plain_map */
ushort *key_maps[MAX_NR_KEYMAPS] = { plain_map };

unsigned int keymap_count = 1;

/* Minimal function key buffer */
char func_buf[8] = {0};
char *funcbufptr = func_buf;
int funcbufsize = sizeof(func_buf);
int funcbufleft = 0;

char *func_table[MAX_NR_FUNC] = { NULL };

struct kbdiacruc accent_table[MAX_DIACR] = { {0, 0, 0} };
unsigned int accent_table_size = 0;
