 
 
#ifndef __LINUX_CONSOLEMAP_H__
#define __LINUX_CONSOLEMAP_H__

#define LAT1_MAP 0
#define GRAF_MAP 1
#define IBMPC_MAP 2
#define USER_MAP 3

#include <linux/types.h>

struct vc_data;

#ifdef CONFIG_CONSOLE_TRANSLATIONS
extern u16 inverse_translate(const struct vc_data *conp, int glyph,
		int use_unicode);
extern unsigned short *set_translate(int m, struct vc_data *vc);
extern int conv_uni_to_pc(struct vc_data *conp, long ucs);
extern u32 conv_8bit_to_uni(unsigned char c);
extern int conv_uni_to_8bit(u32 uni);
void console_map_init(void);
extern int con_set_trans_old(unsigned char __user * arg);
extern int con_get_trans_old(unsigned char __user * arg);
extern int con_set_trans_new(unsigned short __user * arg);
extern int con_get_trans_new(unsigned short __user * arg);
extern int con_clear_unimap(struct vc_data *vc);
struct unipair;
extern int con_get_unimap(struct vc_data *vc, unsigned short ct, unsigned short __user *uct, struct unipair __user *list);
extern int con_set_unimap(struct vc_data *vc, unsigned short ct, struct unipair __user *list);
extern int con_set_default_unimap(struct vc_data *vc);
extern void con_free_unimap(struct vc_data *vc);
#else
static inline u16 inverse_translate(const struct vc_data *conp, int glyph, int use_unicode)
{
	return glyph;
}
static inline unsigned short *set_translate(int m, struct vc_data *vc)
{
	return NULL;
}
static inline int conv_uni_to_pc(struct vc_data *conp, long ucs)
{
	return ucs < 0x100 ? ucs : -1;
}
static inline u32 conv_8bit_to_uni(unsigned char c)
{
	return c;
}
static inline int conv_uni_to_8bit(u32 uni)
{
	return (uni < 0x100) ? uni : -1;
}
static inline void console_map_init(void) { }
static inline int con_set_trans_old(unsigned char __user * arg) { return 0; }
static inline int con_get_trans_old(unsigned char __user * arg) { return -EINVAL; }
static inline int con_set_trans_new(unsigned short __user * arg) { return 0; }
static inline int con_get_trans_new(unsigned short __user * arg) { return -EINVAL; }
static inline int con_clear_unimap(struct vc_data *vc) { return 0; }
struct unipair;
static inline int con_get_unimap(struct vc_data *vc, unsigned short ct, unsigned short __user *uct, struct unipair __user *list) { return 0; }
static inline int con_set_unimap(struct vc_data *vc, unsigned short ct, struct unipair __user *list) { return 0; }
static inline int con_set_default_unimap(struct vc_data *vc) { return 0; }
static inline void con_free_unimap(struct vc_data *vc) { }
#endif

#endif  
