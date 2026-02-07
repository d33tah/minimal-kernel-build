 
 

#ifndef LKC_H
#define LKC_H

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include "expr.h"

#ifdef __cplusplus
extern "C" {
#endif

#include "lkc_proto.h"

#define SRCTREE "srctree"

#ifndef CONFIG_
#define CONFIG_ "CONFIG_"
#endif
static inline const char *CONFIG_prefix(void)
{
	return getenv( "CONFIG_" ) ?: CONFIG_;
}
#undef CONFIG_
#define CONFIG_ CONFIG_prefix()

extern int yylineno;
void zconf_starthelp(void);
FILE *zconf_fopen(const char *name);
void zconf_initscan(const char *name);
void zconf_nextfile(const char *name);
int zconf_lineno(void);
const char *zconf_curname(void);

 
const char *conf_get_configname(void);
void set_all_choice_values(struct symbol *csym);

 
/* xfwrite inlined into expr.c */

 
struct file *file_lookup(const char *name);
void *xmalloc(size_t size);
void *xcalloc(size_t nmemb, size_t size);
void *xrealloc(void *p, size_t size);
char *xstrdup(const char *s);
char *xstrndup(const char *s, size_t n);

 
int yylex(void);

struct gstr {
	size_t len;
	char  *s;
	 
	int max_width;
};
struct gstr str_new(void);
void str_append(struct gstr *gs, const char *s);
void str_printf(struct gstr *gs, const char *fmt, ...);
const char *str_get(struct gstr *gs);

 
void _menu_init(void);
void menu_warn(struct menu *menu, const char *fmt, ...);
struct menu *menu_add_menu(void);
void menu_end_menu(void);
void menu_add_entry(struct symbol *sym);
void menu_add_dep(struct expr *dep);
void menu_add_visibility(struct expr *dep);
struct property *menu_add_prompt(enum prop_type type, char *prompt, struct expr *dep);
void menu_add_expr(enum prop_type type, struct expr *expr, struct expr *dep);
void menu_add_symbol(enum prop_type type, struct symbol *sym, struct expr *dep);
void menu_finalize(struct menu *parent);
void menu_set_type(int type);

extern struct menu rootmenu;

bool menu_is_visible(struct menu *menu);
bool menu_has_prompt(struct menu *menu);
const char *menu_get_prompt(struct menu *menu);

 
void sym_clear_all_valid(void);
struct symbol *sym_choice_default(struct symbol *sym);
struct property *sym_get_range_prop(struct symbol *sym);
struct symbol *sym_check_deps(struct symbol *sym);
struct symbol *prop_get_symbol(struct property *prop);

static inline tristate sym_get_tristate_value(struct symbol *sym)
{
	return sym->curr.tri;
}


static inline bool sym_is_choice(struct symbol *sym)
{
	return sym->flags & SYMBOL_CHOICE ? true : false;
}

static inline bool sym_is_choice_value(struct symbol *sym)
{
	return sym->flags & SYMBOL_CHOICEVAL ? true : false;
}

static inline bool sym_is_optional(struct symbol *sym)
{
	return sym->flags & SYMBOL_OPTIONAL ? true : false;
}

static inline bool sym_has_value(struct symbol *sym)
{
	return sym->flags & SYMBOL_DEF_USER ? true : false;
}

#ifdef __cplusplus
}
#endif

#endif  
