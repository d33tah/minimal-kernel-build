#ifndef _KBD_KERN_H
#define _KBD_KERN_H

#include <linux/tty.h>
#include <linux/interrupt.h>
#include <linux/wait.h>

/* --- 2025-12-06 20:10 --- keyboard.h inlined (45 LOC) */
#define KG_SHIFT	0
#define KG_CTRL		2
#define KG_ALT		3
#define KG_ALTGR	1
#define KG_SHIFTL	4
#define KG_SHIFTR	5
#define KG_CTRLL	6
#define KG_CTRLR	7
#define KG_CAPSSHIFT	8
#define NR_SHIFT	9
#define NR_KEYS		256
#define MAX_NR_KEYMAPS	256
#define MAX_NR_OF_USER_KEYMAPS 256
#define MAX_NR_FUNC	256
#define MAX_DIACR	256
#define KT_LATIN	0
#define KT_FN		1
#define KT_SPEC		2
#define KT_PAD		3
#define KT_DEAD		4
#define KT_CONS		5
#define KT_CUR		6
#define KT_SHIFT	7
#define KT_META		8
#define KT_ASCII	9
#define KT_LOCK		10
#define KT_LETTER	11
#define KT_SLOCK	12
#define KT_DEAD2	13
#define KT_BRL		14
#define K(t,v)		(((t)<<8)|(v))
#define KTYP(x)		((x) >> 8)
#define KVAL(x)		((x) & 0xff)
extern unsigned short *key_maps[MAX_NR_KEYMAPS];
extern unsigned short plain_map[NR_KEYS];
/* --- end keyboard.h inlined --- */



extern int kbd_init(void);

extern int set_console(int nr);

#define U(x) ((x) ^ 0xf000)

#define BRL_UC_ROW 0x2800


struct console;

void vt_set_leds_compute_shiftstate(void);

#endif
