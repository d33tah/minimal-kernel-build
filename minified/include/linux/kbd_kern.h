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

extern char *func_table[MAX_NR_FUNC];

struct kbd_struct {

	unsigned char lockstate;
#define VC_SHIFTLOCK	KG_SHIFT	 
#define VC_ALTGRLOCK	KG_ALTGR	 
#define VC_CTRLLOCK	KG_CTRL 	 
#define VC_ALTLOCK	KG_ALT  	 
#define VC_SHIFTLLOCK	KG_SHIFTL	 
#define VC_SHIFTRLOCK	KG_SHIFTR	 
#define VC_CTRLLLOCK	KG_CTRLL 	 
#define VC_CTRLRLOCK	KG_CTRLR 	 
	unsigned char slockstate; 	 

	unsigned char ledmode:1;
#define LED_SHOW_FLAGS 0         
#define LED_SHOW_IOCTL 1         

	unsigned char ledflagstate:4;	 
	unsigned char default_ledflagstate:4;
#define VC_SCROLLOCK	0	 
#define VC_NUMLOCK	1	 
#define VC_CAPSLOCK	2	 
#define VC_KANALOCK	3	 

	unsigned char kbdmode:3;	 
#define VC_XLATE	0	 
#define VC_MEDIUMRAW	1	 
#define VC_RAW		2	 
#define VC_UNICODE	3	 
#define VC_OFF		4	 

	unsigned char modeflags:5;
#define VC_APPLIC	0	 
#define VC_CKMODE	1	 
#define VC_REPEAT	2	 
#define VC_CRLF		3	 
#define VC_META		4	 
};

extern int kbd_init(void);

extern void setledstate(struct kbd_struct *kbd, unsigned int led);

extern int do_poke_blanked_console;

extern void (*kbd_ledfunc)(unsigned int led);

extern int set_console(int nr);
extern void schedule_console_callback(void);

static inline int vc_kbd_mode(struct kbd_struct * kbd, int flag)
{
	return ((kbd->modeflags >> flag) & 1);
}

static inline int vc_kbd_led(struct kbd_struct * kbd, int flag)
{
	return ((kbd->ledflagstate >> flag) & 1);
}

static inline void set_vc_kbd_mode(struct kbd_struct * kbd, int flag)
{
	kbd->modeflags |= 1 << flag;
}

static inline void set_vc_kbd_led(struct kbd_struct * kbd, int flag)
{
	kbd->ledflagstate |= 1 << flag;
}

static inline void clr_vc_kbd_mode(struct kbd_struct * kbd, int flag)
{
	kbd->modeflags &= ~(1 << flag);
}

static inline void clr_vc_kbd_led(struct kbd_struct * kbd, int flag)
{
	kbd->ledflagstate &= ~(1 << flag);
}

static inline void chg_vc_kbd_lock(struct kbd_struct * kbd, int flag)
{
	kbd->lockstate ^= 1 << flag;
}

static inline void chg_vc_kbd_slock(struct kbd_struct * kbd, int flag)
{
	kbd->slockstate ^= 1 << flag;
}

static inline void chg_vc_kbd_mode(struct kbd_struct * kbd, int flag)
{
	kbd->modeflags ^= 1 << flag;
}

static inline void chg_vc_kbd_led(struct kbd_struct * kbd, int flag)
{
	kbd->ledflagstate ^= 1 << flag;
}

#define U(x) ((x) ^ 0xf000)

#define BRL_UC_ROW 0x2800


struct console;

void vt_set_leds_compute_shiftstate(void);


extern unsigned int keymap_count;

#endif
