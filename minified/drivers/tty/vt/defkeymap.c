// SPDX-License-Identifier: GPL-2.0
/* Minimal stubbed defkeymap for minimal kernel */

#include <linux/types.h>
#include <linux/keyboard.h>
#include <linux/kd.h>

unsigned short plain_map[NR_KEYS] = {0};
static unsigned short shift_map[NR_KEYS] = {0};
static unsigned short altgr_map[NR_KEYS] = {0};
static unsigned short ctrl_map[NR_KEYS] = {0};
static unsigned short shift_ctrl_map[NR_KEYS] = {0};
static unsigned short alt_map[NR_KEYS] = {0};
static unsigned short ctrl_alt_map[NR_KEYS] = {0};

ushort *key_maps[MAX_NR_KEYMAPS] = {
	plain_map, shift_map, altgr_map, NULL,
	ctrl_map, shift_ctrl_map, NULL, NULL,
	alt_map, NULL, NULL, NULL,
	ctrl_alt_map, NULL
};

unsigned int keymap_count = 7;
char func_buf[] = {0};
char *funcbufptr = func_buf;
int funcbufsize = sizeof(func_buf);
int funcbufleft = 0;
char *func_table[MAX_NR_FUNC] = {NULL};
struct kbdiacruc accent_table[MAX_DIACR] = {{0}};
unsigned int accent_table_size = 0;
