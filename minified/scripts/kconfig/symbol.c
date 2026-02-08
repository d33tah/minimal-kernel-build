
#include <sys/types.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <regex.h>

#include "lkc.h"

struct symbol symbol_yes = {
	.name = "y",
	.curr = { "y", yes },
	.flags = SYMBOL_CONST | SYMBOL_VALID,
};

struct symbol symbol_mod = {
	.name = "m",
	.curr = { "m", mod },
	.flags = SYMBOL_CONST | SYMBOL_VALID,
};

struct symbol symbol_no = {
	.name = "n",
	.curr = { "n", no },
	.flags = SYMBOL_CONST | SYMBOL_VALID,
};

static struct symbol symbol_empty = {
	.name = "",
	.curr = { "", no },
	.flags = SYMBOL_VALID,
};

struct symbol *modules_sym;
static tristate modules_val;

enum symbol_type sym_get_type(struct symbol *sym)
{
	enum symbol_type type = sym->type;

	if (type == S_TRISTATE) {
		if (sym_is_choice_value(sym) && sym->visible == yes)
			type = S_BOOLEAN;
		else if (modules_val == no)
			type = S_BOOLEAN;
	}
	return type;
}

const char *sym_type_name(enum symbol_type type)
{
	switch (type) {
	case S_BOOLEAN:
		return "bool";
	case S_TRISTATE:
		return "tristate";
	case S_INT:
		return "integer";
	case S_HEX:
		return "hex";
	case S_STRING:
		return "string";
	case S_UNKNOWN:
		return "unknown";
	}
	return "???";
}

struct property *sym_get_choice_prop(struct symbol *sym)
{
	struct property *prop;

	for_all_choices(sym, prop) return prop;
	return NULL;
}

static struct property *sym_get_default_prop(struct symbol *sym)
{
	struct property *prop;

	for_all_defaults(sym, prop)
	{
		prop->visible.tri = expr_calc_value(prop->visible.expr);
		if (prop->visible.tri != no)
			return prop;
	}
	return NULL;
}

struct property *sym_get_range_prop(struct symbol *sym)
{
	struct property *prop;

	for_all_properties(sym, prop, P_RANGE)
	{
		prop->visible.tri = expr_calc_value(prop->visible.expr);
		if (prop->visible.tri != no)
			return prop;
	}
	return NULL;
}

static long long sym_get_range_val(struct symbol *sym, int base)
{
	sym_calc_value(sym);
	switch (sym->type) {
	case S_INT:
		base = 10;
		break;
	case S_HEX:
		base = 16;
		break;
	default:
		break;
	}
	return strtoll(sym->curr.val, NULL, base);
}

static void sym_validate_range(struct symbol *sym)
{
	struct property *prop;
	int base;
	long long val, val2;
	char str[64];

	switch (sym->type) {
	case S_INT:
		base = 10;
		break;
	case S_HEX:
		base = 16;
		break;
	default:
		return;
	}
	prop = sym_get_range_prop(sym);
	if (!prop)
		return;
	val = strtoll(sym->curr.val, NULL, base);
	val2 = sym_get_range_val(prop->expr->left.sym, base);
	if (val >= val2) {
		val2 = sym_get_range_val(prop->expr->right.sym, base);
		if (val <= val2)
			return;
	}
	if (sym->type == S_INT)
		sprintf(str, "%lld", val2);
	else
		sprintf(str, "0x%llx", val2);
	sym->curr.val = xstrdup(str);
}

static void sym_set_changed(struct symbol *sym)
{
	struct property *prop;

	sym->flags |= SYMBOL_CHANGED;
	for (prop = sym->prop; prop; prop = prop->next) {
		if (prop->menu)
			prop->menu->flags |= MENU_CHANGED;
	}
}

static void sym_set_all_changed(void)
{
	struct symbol *sym;
	int i;

	for_all_symbols(i, sym) sym_set_changed(sym);
}

static void sym_calc_visibility(struct symbol *sym)
{
	struct property *prop;
	struct symbol *choice_sym = NULL;
	tristate tri;

	tri = no;

	if (sym_is_choice_value(sym))
		choice_sym = prop_get_symbol(sym_get_choice_prop(sym));

	for_all_prompts(sym, prop)
	{
		prop->visible.tri = expr_calc_value(prop->visible.expr);

		if (choice_sym && sym->type == S_TRISTATE &&
		    prop->visible.tri == mod && choice_sym->curr.tri == yes)
			prop->visible.tri = no;

		tri = EXPR_OR(tri, prop->visible.tri);
	}
	if (tri == mod && (sym->type != S_TRISTATE || modules_val == no))
		tri = yes;
	if (sym->visible != tri) {
		sym->visible = tri;
		sym_set_changed(sym);
	}
	if (sym_is_choice_value(sym))
		return;

	tri = yes;
	if (sym->dir_dep.expr)
		tri = expr_calc_value(sym->dir_dep.expr);
	if (tri == mod && sym_get_type(sym) == S_BOOLEAN)
		tri = yes;
	if (sym->dir_dep.tri != tri) {
		sym->dir_dep.tri = tri;
		sym_set_changed(sym);
	}
	tri = no;
	if (sym->rev_dep.expr)
		tri = expr_calc_value(sym->rev_dep.expr);
	if (tri == mod && sym_get_type(sym) == S_BOOLEAN)
		tri = yes;
	if (sym->rev_dep.tri != tri) {
		sym->rev_dep.tri = tri;
		sym_set_changed(sym);
	}
	tri = no;
	if (sym->implied.expr)
		tri = expr_calc_value(sym->implied.expr);
	if (tri == mod && sym_get_type(sym) == S_BOOLEAN)
		tri = yes;
	if (sym->implied.tri != tri) {
		sym->implied.tri = tri;
		sym_set_changed(sym);
	}
}

struct symbol *sym_choice_default(struct symbol *sym)
{
	struct symbol *def_sym;
	struct property *prop;
	struct expr *e;

	for_all_defaults(sym, prop)
	{
		prop->visible.tri = expr_calc_value(prop->visible.expr);
		if (prop->visible.tri == no)
			continue;
		def_sym = prop_get_symbol(prop);
		if (def_sym->visible != no)
			return def_sym;
	}

	prop = sym_get_choice_prop(sym);
	expr_list_for_each_sym(prop->expr, e, def_sym) if (def_sym->visible !=
							   no) return def_sym;

	return NULL;
}

static struct symbol *sym_calc_choice(struct symbol *sym)
{
	struct symbol *def_sym;
	struct property *prop;
	struct expr *e;
	int flags;

	flags = sym->flags;
	prop = sym_get_choice_prop(sym);
	expr_list_for_each_sym(prop->expr, e, def_sym)
	{
		sym_calc_visibility(def_sym);
		if (def_sym->visible != no)
			flags &= def_sym->flags;
	}

	sym->flags &= flags | ~SYMBOL_DEF_USER;

	def_sym = sym->def[S_DEF_USER].val;
	if (def_sym && def_sym->visible != no)
		return def_sym;

	def_sym = sym_choice_default(sym);

	if (def_sym == NULL)

		sym->curr.tri = no;

	return def_sym;
}

static void sym_warn_unmet_dep(struct symbol *sym)
{
	fprintf(stderr,
		"\nWARNING: unmet direct dependencies detected for %s\n"
		"  Depends on [%c]\n",
		sym->name, sym->dir_dep.tri == mod ? 'm' : 'n');
}

void sym_calc_value(struct symbol *sym)
{
	struct symbol_value newval, oldval;
	struct property *prop;
	struct expr *e;

	if (!sym)
		return;

	if (sym->flags & SYMBOL_VALID)
		return;

	if (sym_is_choice_value(sym) &&
	    sym->flags & SYMBOL_NEED_SET_CHOICE_VALUES) {
		sym->flags &= ~SYMBOL_NEED_SET_CHOICE_VALUES;
		prop = sym_get_choice_prop(sym);
		sym_calc_value(prop_get_symbol(prop));
	}

	sym->flags |= SYMBOL_VALID;

	oldval = sym->curr;

	switch (sym->type) {
	case S_INT:
	case S_HEX:
	case S_STRING:
		newval = symbol_empty.curr;
		break;
	case S_BOOLEAN:
	case S_TRISTATE:
		newval = symbol_no.curr;
		break;
	default:
		sym->curr.val = sym->name;
		sym->curr.tri = no;
		return;
	}
	sym->flags &= ~SYMBOL_WRITE;

	sym_calc_visibility(sym);

	if (sym->visible != no)
		sym->flags |= SYMBOL_WRITE;

	sym->curr = newval;

	switch (sym_get_type(sym)) {
	case S_BOOLEAN:
	case S_TRISTATE:
		if (sym_is_choice_value(sym) && sym->visible == yes) {
			prop = sym_get_choice_prop(sym);
			newval.tri = (prop_get_symbol(prop)->curr.val == sym) ?
					     yes :
					     no;
		} else {
			if (sym->visible != no) {
				if (sym_has_value(sym)) {
					newval.tri = EXPR_AND(
						sym->def[S_DEF_USER].tri,
						sym->visible);
					goto calc_newval;
				}
			}
			if (sym->rev_dep.tri != no)
				sym->flags |= SYMBOL_WRITE;
			if (!sym_is_choice(sym)) {
				prop = sym_get_default_prop(sym);
				if (prop) {
					newval.tri = EXPR_AND(
						expr_calc_value(prop->expr),
						prop->visible.tri);
					if (newval.tri != no)
						sym->flags |= SYMBOL_WRITE;
				}
				if (sym->implied.tri != no) {
					sym->flags |= SYMBOL_WRITE;
					newval.tri = EXPR_OR(newval.tri,
							     sym->implied.tri);
					newval.tri = EXPR_AND(newval.tri,
							      sym->dir_dep.tri);
				}
			}
calc_newval:
			if (sym->dir_dep.tri < sym->rev_dep.tri)
				sym_warn_unmet_dep(sym);
			newval.tri = EXPR_OR(newval.tri, sym->rev_dep.tri);
		}
		if (newval.tri == mod && sym_get_type(sym) == S_BOOLEAN)
			newval.tri = yes;
		break;
	case S_STRING:
	case S_HEX:
	case S_INT:
		if (sym->visible != no && sym_has_value(sym)) {
			newval.val = sym->def[S_DEF_USER].val;
			break;
		}
		prop = sym_get_default_prop(sym);
		if (prop) {
			struct symbol *ds = prop_get_symbol(prop);
			if (ds) {
				sym->flags |= SYMBOL_WRITE;
				sym_calc_value(ds);
				newval.val = ds->curr.val;
			}
		}
		break;
	default:;
	}

	sym->curr = newval;
	if (sym_is_choice(sym) && newval.tri == yes)
		sym->curr.val = sym_calc_choice(sym);
	sym_validate_range(sym);

	if (memcmp(&oldval, &sym->curr, sizeof(oldval))) {
		sym_set_changed(sym);
		if (modules_sym == sym) {
			sym_set_all_changed();
			modules_val = modules_sym->curr.tri;
		}
	}

	if (sym_is_choice(sym)) {
		struct symbol *choice_sym;

		prop = sym_get_choice_prop(sym);
		expr_list_for_each_sym(prop->expr, e, choice_sym)
		{
			if ((sym->flags & SYMBOL_WRITE) &&
			    choice_sym->visible != no)
				choice_sym->flags |= SYMBOL_WRITE;
			if (sym->flags & SYMBOL_CHANGED)
				sym_set_changed(choice_sym);
		}
	}

	if (sym->flags & SYMBOL_NO_WRITE)
		sym->flags &= ~SYMBOL_WRITE;

	if (sym->flags & SYMBOL_NEED_SET_CHOICE_VALUES)
		set_all_choice_values(sym);
}

void sym_clear_all_valid(void)
{
	struct symbol *sym;
	int i;

	for_all_symbols(i, sym) sym->flags &= ~SYMBOL_VALID;
	conf_set_changed(true);
	sym_calc_value(modules_sym);
}

bool sym_tristate_within_range(struct symbol *sym, tristate val)
{
	int type = sym_get_type(sym);

	if (sym->visible == no)
		return false;

	if (type != S_BOOLEAN && type != S_TRISTATE)
		return false;

	if (type == S_BOOLEAN && val == mod)
		return false;
	if (sym->visible <= sym->rev_dep.tri)
		return false;
	if (sym_is_choice_value(sym) && sym->visible == yes)
		return val == yes;
	return val >= sym->rev_dep.tri && val <= sym->visible;
}

bool sym_string_valid(struct symbol *sym, const char *str)
{
	signed char ch;

	switch (sym->type) {
	case S_STRING:
		return true;
	case S_INT:
		ch = *str++;
		if (ch == '-')
			ch = *str++;
		if (!isdigit(ch))
			return false;
		if (ch == '0' && *str != 0)
			return false;
		while ((ch = *str++)) {
			if (!isdigit(ch))
				return false;
		}
		return true;
	case S_HEX:
		if (str[0] == '0' && (str[1] == 'x' || str[1] == 'X'))
			str += 2;
		ch = *str++;
		do {
			if (!isxdigit(ch))
				return false;
		} while ((ch = *str++));
		return true;
	case S_BOOLEAN:
	case S_TRISTATE:
		switch (str[0]) {
		case 'y':
		case 'Y':
		case 'm':
		case 'M':
		case 'n':
		case 'N':
			return true;
		}
		return false;
	default:
		return false;
	}
}

bool sym_string_within_range(struct symbol *sym, const char *str)
{
	struct property *prop;
	long long val;

	switch (sym->type) {
	case S_STRING:
		return sym_string_valid(sym, str);
	case S_INT:
		if (!sym_string_valid(sym, str))
			return false;
		prop = sym_get_range_prop(sym);
		if (!prop)
			return true;
		val = strtoll(str, NULL, 10);
		return val >= sym_get_range_val(prop->expr->left.sym, 10) &&
		       val <= sym_get_range_val(prop->expr->right.sym, 10);
	case S_HEX:
		if (!sym_string_valid(sym, str))
			return false;
		prop = sym_get_range_prop(sym);
		if (!prop)
			return true;
		val = strtoll(str, NULL, 16);
		return val >= sym_get_range_val(prop->expr->left.sym, 16) &&
		       val <= sym_get_range_val(prop->expr->right.sym, 16);
	case S_BOOLEAN:
	case S_TRISTATE:
		switch (str[0]) {
		case 'y':
		case 'Y':
			return sym_tristate_within_range(sym, yes);
		case 'm':
		case 'M':
			return sym_tristate_within_range(sym, mod);
		case 'n':
		case 'N':
			return sym_tristate_within_range(sym, no);
		}
		return false;
	default:
		return false;
	}
}

const char *sym_get_string_value(struct symbol *sym)
{
	tristate val;

	switch (sym->type) {
	case S_BOOLEAN:
	case S_TRISTATE:
		val = sym_get_tristate_value(sym);
		switch (val) {
		case no:
			return "n";
		case mod:
			sym_calc_value(modules_sym);
			return (modules_sym->curr.tri == no) ? "n" : "m";
		case yes:
			return "y";
		}
		break;
	default:;
	}
	return (const char *)sym->curr.val;
}

static unsigned strhash(const char *s)
{
	unsigned hash = 2166136261U;
	for (; *s; s++)
		hash = (hash ^ *s) * 0x01000193;
	return hash;
}

struct symbol *sym_lookup(const char *name, int flags)
{
	struct symbol *symbol;
	char *new_name;
	int hash;

	if (name) {
		if (name[0] && !name[1]) {
			switch (name[0]) {
			case 'y':
				return &symbol_yes;
			case 'm':
				return &symbol_mod;
			case 'n':
				return &symbol_no;
			}
		}
		hash = strhash(name) % SYMBOL_HASHSIZE;

		for (symbol = symbol_hash[hash]; symbol;
		     symbol = symbol->next) {
			if (symbol->name && !strcmp(symbol->name, name) &&
			    (flags ? symbol->flags & flags :
				     !(symbol->flags &
				       (SYMBOL_CONST | SYMBOL_CHOICE))))
				return symbol;
		}
		new_name = xstrdup(name);
	} else {
		new_name = NULL;
		hash = 0;
	}

	symbol = xmalloc(sizeof(*symbol));
	memset(symbol, 0, sizeof(*symbol));
	symbol->name = new_name;
	symbol->type = S_UNKNOWN;
	symbol->flags = flags;

	symbol->next = symbol_hash[hash];
	symbol_hash[hash] = symbol;

	return symbol;
}

struct symbol *sym_find(const char *name)
{
	struct symbol *symbol = NULL;
	int hash = 0;

	if (!name)
		return NULL;

	if (name[0] && !name[1]) {
		switch (name[0]) {
		case 'y':
			return &symbol_yes;
		case 'm':
			return &symbol_mod;
		case 'n':
			return &symbol_no;
		}
	}
	hash = strhash(name) % SYMBOL_HASHSIZE;

	for (symbol = symbol_hash[hash]; symbol; symbol = symbol->next) {
		if (symbol->name && !strcmp(symbol->name, name) &&
		    !(symbol->flags & SYMBOL_CONST))
			break;
	}

	return symbol;
}

/* dep_stack, dep_stack_insert, dep_stack_remove, sym_check_print_recursive,
   sym_check_expr_deps, sym_check_sym_deps, sym_check_choice_deps,
   sym_check_deps removed - recursive dependency validation not needed
   for minimal tinyconfig build (~265 LOC) */

struct symbol *prop_get_symbol(struct property *prop)
{
	if (prop->expr &&
	    (prop->expr->type == E_SYMBOL || prop->expr->type == E_LIST))
		return prop->expr->left.sym;
	return NULL;
}

/* prop_get_type_name removed - only used by removed sym_check_print_recursive */
