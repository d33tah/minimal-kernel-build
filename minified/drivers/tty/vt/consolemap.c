// SPDX-License-Identifier: GPL-2.0
/*
 * consolemap.c - Minimal console character mapping for Hello World kernel
 */

#include <linux/module.h>
#include <linux/kd.h>
#include <linux/errno.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/tty.h>
#include <linux/uaccess.h>
#include <linux/console.h>
#include <linux/consolemap.h>
#include <linux/vt_kern.h>
#include <linux/string.h>

/* Minimal translation tables - only Latin-1 identity mapping needed for ASCII */
static unsigned short translations[][256] = {
  {0}, /* LAT1_MAP - zero init, will work for ASCII */
  {0}, /* GRAF_MAP */
  {0}, /* IBMPC_MAP */
  {0}  /* USER_MAP */
};

#define MAX_GLYPH 512
static int inv_translate[MAX_NR_CONSOLES];

struct uni_pagedir {
	u16 		**uni_pgdir[32];
	unsigned long	refcount;
	unsigned long	sum;
	unsigned char	*inverse_translations[4];
	u16		*inverse_trans_unicode;
};

static struct uni_pagedir *dflt;

unsigned short *set_translate(int m, struct vc_data *vc)
{
	inv_translate[vc->vc_num] = m;
	return translations[m];
}

u16 inverse_translate(const struct vc_data *conp, int glyph, int use_unicode)
{
	if (glyph < 0 || glyph >= MAX_GLYPH)
		return 0;
	return glyph; /* Identity mapping */
}

int con_set_trans_old(unsigned char __user * arg)
{
	return 0; /* Stub */
}

int con_get_trans_old(unsigned char __user * arg)
{
	return -EINVAL; /* Stub */
}

int con_set_trans_new(ushort __user * arg)
{
	return 0; /* Stub */
}

int con_get_trans_new(ushort __user * arg)
{
	return -EINVAL; /* Stub */
}

extern u8 dfont_unicount[];
extern u16 dfont_unitable[];

static void con_release_unimap(struct uni_pagedir *p)
{
	int i;
	if (p == dflt) dflt = NULL;
	for (i = 0; i < 32; i++) {
		if (p->uni_pgdir[i]) {
			int j;
			for (j = 0; j < 32; j++)
				kfree(p->uni_pgdir[i][j]);
			kfree(p->uni_pgdir[i]);
			p->uni_pgdir[i] = NULL;
		}
	}
	for (i = 0; i < 4; i++) {
		kfree(p->inverse_translations[i]);
		p->inverse_translations[i] = NULL;
	}
	kfree(p->inverse_trans_unicode);
	p->inverse_trans_unicode = NULL;
}

void con_free_unimap(struct vc_data *vc)
{
	struct uni_pagedir *p = *vc->vc_uni_pagedir_loc;
	if (!p)
		return;
	*vc->vc_uni_pagedir_loc = NULL;
	if (--p->refcount)
		return;
	con_release_unimap(p);
	kfree(p);
}

static int con_do_clear_unimap(struct vc_data *vc)
{
	struct uni_pagedir *p = *vc->vc_uni_pagedir_loc;
	if (!p || --p->refcount) {
		struct uni_pagedir *q = kzalloc(sizeof(*p), GFP_KERNEL);
		if (!q) {
			if (p)
				p->refcount++;
			return -ENOMEM;
		}
		q->refcount=1;
		*vc->vc_uni_pagedir_loc = q;
	} else {
		if (p == dflt) dflt = NULL;
		p->refcount++;
		p->sum = 0;
		con_release_unimap(p);
	}
	return 0;
}

int con_clear_unimap(struct vc_data *vc)
{
	int ret;
	console_lock();
	ret = con_do_clear_unimap(vc);
	console_unlock();
	return ret;
}

int con_set_unimap(struct vc_data *vc, ushort ct, struct unipair __user *list)
{
	return 0; /* Stub */
}

int con_set_default_unimap(struct vc_data *vc)
{
	return 0; /* Stub */
}

int con_copy_unimap(struct vc_data *dst_vc, struct vc_data *src_vc)
{
	struct uni_pagedir *q;
	if (!*src_vc->vc_uni_pagedir_loc)
		return -EINVAL;
	if (*dst_vc->vc_uni_pagedir_loc == *src_vc->vc_uni_pagedir_loc)
		return 0;
	con_free_unimap(dst_vc);
	q = *src_vc->vc_uni_pagedir_loc;
	q->refcount++;
	*dst_vc->vc_uni_pagedir_loc = q;
	return 0;
}

int con_get_unimap(struct vc_data *vc, ushort ct, ushort __user *uct, struct unipair __user *list)
{
	put_user(0, uct);
	return 0; /* Stub */
}

u32 conv_8bit_to_uni(unsigned char c)
{
	return c; /* Identity mapping for ASCII */
}

int conv_uni_to_8bit(u32 uni)
{
	return (uni < 0x100) ? uni : -1;
}

int conv_uni_to_pc(struct vc_data *conp, long ucs)
{
	if (ucs > 0xffff)
		return -4;
	if (ucs < 0x20)
		return -1;
	if (ucs == 0xfeff || (ucs >= 0x200b && ucs <= 0x200f))
		return -2;
	if ((ucs & ~UNI_DIRECT_MASK) == UNI_DIRECT_BASE)
		return ucs & UNI_DIRECT_MASK;
	return (ucs < 0x100) ? ucs : -4; /* Simple identity for low ASCII */
}

void __init console_map_init(void)
{
	int i;
	for (i = 0; i < MAX_NR_CONSOLES; i++)
		if (vc_cons_allocated(i) && !*vc_cons[i].d->vc_uni_pagedir_loc)
			con_set_default_unimap(vc_cons[i].d);
}
