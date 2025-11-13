// SPDX-License-Identifier: GPL-2.0
/*
 * Minimal keyboard map stub - keyboard input is not used
 * Arrays kept to minimum size to satisfy linker requirements
 */

#include <linux/types.h>
#include <linux/keyboard.h>
#include <linux/kd.h>

/* Minimal plain_map - only need to define the array */
unsigned short plain_map[NR_KEYS] = { 0 };

/* All other keymaps are zero-filled - use compound literals to save space */
static unsigned short shift_map[NR_KEYS];
static unsigned short altgr_map[NR_KEYS];
static unsigned short ctrl_map[NR_KEYS];
static unsigned short shift_ctrl_map[NR_KEYS];
static unsigned short alt_map[NR_KEYS];
static unsigned short ctrl_alt_map[NR_KEYS];

ushort *key_maps[MAX_NR_KEYMAPS] = {
	plain_map, shift_map, altgr_map, NULL,
	ctrl_map, shift_ctrl_map, NULL, NULL,
	alt_map, NULL, NULL, NULL,
	ctrl_alt_map, NULL
};

unsigned int keymap_count = 7;

/* Minimal function buffer */
char func_buf[8] = { 0 };
char *funcbufptr = func_buf;
int funcbufsize = sizeof(func_buf);
int funcbufleft = 0;

char *func_table[MAX_NR_FUNC];

struct kbdiacruc accent_table[MAX_DIACR];
unsigned int accent_table_size = 0;
