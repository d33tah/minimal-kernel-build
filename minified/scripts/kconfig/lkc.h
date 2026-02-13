 
#ifndef LKC_H
#define LKC_H

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

/* expr.h inlined - single includer */
#include "list.h"
#ifndef __cplusplus
#include <stdbool.h>
#endif

struct file {
	struct file *next;
	struct file *parent;
	const char *name;
	int lineno;
};

typedef enum tristate {
	no, mod, yes
} tristate;

enum expr_type {
	E_NONE, E_OR, E_AND, E_NOT,
	E_EQUAL, E_UNEQUAL, E_LTH, E_LEQ, E_GTH, E_GEQ,
	E_LIST, E_SYMBOL, E_RANGE
};

union expr_data {
	struct expr *expr;
	struct symbol *sym;
};

struct expr {
	enum expr_type type;
	union expr_data left, right;
};

#define EXPR_OR(dep1, dep2)	(((dep1)>(dep2))?(dep1):(dep2))
#define EXPR_AND(dep1, dep2)	(((dep1)<(dep2))?(dep1):(dep2))
#define EXPR_NOT(dep)		(2-(dep))

#define expr_list_for_each_sym(l, e, s) \
	for (e = (l); e && (s = e->right.sym); e = e->left.expr)

struct expr_value {
	struct expr *expr;
	tristate tri;
};

struct symbol_value {
	void *val;
	tristate tri;
};

enum symbol_type {
	S_UNKNOWN, S_BOOLEAN, S_TRISTATE, S_INT, S_HEX, S_STRING
};

enum {
	S_DEF_USER,
	S_DEF_AUTO,
	S_DEF_DEF3,
	S_DEF_DEF4,
	S_DEF_COUNT
};

struct symbol {
	struct symbol *next;
	char *name;
	enum symbol_type type;
	struct symbol_value curr;
	struct symbol_value def[S_DEF_COUNT];
	tristate visible;
	int flags;
	struct property *prop;
	struct expr_value dir_dep;
	struct expr_value rev_dep;
	struct expr_value implied;
};

#define for_all_symbols(i, sym) for (i = 0; i < SYMBOL_HASHSIZE; i++) for (sym = symbol_hash[i]; sym; sym = sym->next)

#define SYMBOL_CONST      0x0001
#define SYMBOL_CHOICE     0x0010
#define SYMBOL_CHOICEVAL  0x0020
#define SYMBOL_VALID      0x0080
#define SYMBOL_OPTIONAL   0x0100
#define SYMBOL_WRITE      0x0200
#define SYMBOL_CHANGED    0x0400
#define SYMBOL_WRITTEN    0x0800
#define SYMBOL_NO_WRITE   0x1000
#define SYMBOL_WARNED     0x8000

#define SYMBOL_DEF        0x10000
#define SYMBOL_DEF_USER   0x10000
#define SYMBOL_NEED_SET_CHOICE_VALUES  0x100000
#define SYMBOL_HASHSIZE		9973

enum prop_type {
	P_UNKNOWN,
	P_PROMPT,
	P_COMMENT,
	P_MENU,
	P_DEFAULT,
	P_CHOICE,
	P_SELECT,
	P_IMPLY,
	P_RANGE,
	P_SYMBOL,
};

struct property {
	struct property *next;
	enum prop_type type;
	const char *text;
	struct expr_value visible;
	struct expr *expr;
	struct menu *menu;
	struct file *file;
	int lineno;
};

#define for_all_properties(sym, st, tok) \
	for (st = sym->prop; st; st = st->next) \
		if (st->type == (tok))
#define for_all_defaults(sym, st) for_all_properties(sym, st, P_DEFAULT)
#define for_all_choices(sym, st) for_all_properties(sym, st, P_CHOICE)
#define for_all_prompts(sym, st) \
	for (st = sym->prop; st; st = st->next) \
		if (st->text)

struct menu {
	struct menu *next;
	struct menu *parent;
	struct menu *list;
	struct symbol *sym;
	struct property *prompt;
	struct expr *visibility;
	struct expr *dep;
	unsigned int flags;
	char *help;
	struct file *file;
	int lineno;
	void *data;
};

#define MENU_CHANGED		0x0001

extern struct file *file_list;
extern struct file *current_file;

extern struct symbol symbol_yes, symbol_no, symbol_mod;
extern struct symbol *modules_sym;
struct expr *expr_alloc_symbol(struct symbol *sym);
struct expr *expr_alloc_one(enum expr_type type, struct expr *ce);
struct expr *expr_alloc_two(enum expr_type type, struct expr *e1, struct expr *e2);
struct expr *expr_alloc_comp(enum expr_type type, struct symbol *s1, struct symbol *s2);
struct expr *expr_alloc_and(struct expr *e1, struct expr *e2);
struct expr *expr_alloc_or(struct expr *e1, struct expr *e2);
struct expr *expr_copy(const struct expr *org);
void expr_free(struct expr *e);
void expr_eliminate_eq(struct expr **ep1, struct expr **ep2);
tristate expr_calc_value(struct expr *e);
struct expr *expr_trans_bool(struct expr *e);
struct expr *expr_eliminate_dups(struct expr *e);
struct expr *expr_transform(struct expr *e);
int expr_contains_symbol(struct expr *dep, struct symbol *sym);
bool expr_depends_symbol(struct expr *dep, struct symbol *sym);
struct expr *expr_trans_compare(struct expr *e, enum expr_type type, struct symbol *sym);

static inline int expr_is_yes(struct expr *e)
{
	return !e || (e->type == E_SYMBOL && e->left.sym == &symbol_yes);
}
/* end expr.h inline */

#ifdef __cplusplus
extern "C" {
#endif

/* lkc_proto.h inlined - single includer */
#include <stdarg.h>

void conf_parse(const char *name);
int conf_read(const char *name);
int conf_read_simple(const char *name, int);
int conf_write(const char *name);
int conf_write_autoconf(int overwrite);
void conf_set_changed(bool val);
bool conf_get_changed(void);
void conf_set_message_callback(void (*fn)(const char *s));

extern struct symbol * symbol_hash[SYMBOL_HASHSIZE];

struct symbol * sym_lookup(const char *name, int flags);
struct symbol * sym_find(const char *name);
void sym_calc_value(struct symbol *sym);
enum symbol_type sym_get_type(struct symbol *sym);
bool sym_tristate_within_range(struct symbol *sym,tristate tri);
bool sym_string_valid(struct symbol *sym, const char *newval);
bool sym_string_within_range(struct symbol *sym, const char *str);
struct property * sym_get_choice_prop(struct symbol *sym);
const char * sym_get_string_value(struct symbol *sym);

enum variable_flavor {
	VAR_SIMPLE,
	VAR_RECURSIVE,
	VAR_APPEND,
};
void env_write_dep(FILE *f, const char *auto_conf_name);
void variable_add(const char *name, const char *value,
		  enum variable_flavor flavor);
void variable_all_del(void);
char *expand_dollar(const char **str);
char *expand_one_token(const char **str);
/* end lkc_proto.h inline */

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
FILE *zconf_fopen(const char *name);
void zconf_initscan(const char *name);
void zconf_nextfile(const char *name);
int zconf_lineno(void);
const char *zconf_curname(void);

const char *conf_get_configname(void);
void set_all_choice_values(struct symbol *csym);

struct file *file_lookup(const char *name);
void *xmalloc(size_t size);
void *xcalloc(size_t nmemb, size_t size);
void *xrealloc(void *p, size_t size);
char *xstrdup(const char *s);
char *xstrndup(const char *s, size_t n);

void _menu_init(void);
struct menu *menu_add_menu(void);
void menu_end_menu(void);
void menu_add_entry(struct symbol *sym);
void menu_add_dep(struct expr *dep);
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
