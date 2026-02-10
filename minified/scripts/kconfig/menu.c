
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "lkc.h"
#include "internal.h"

/* menu_warn, prop_warn, sym_check_prop, menu_validate_number removed -
   validation warnings not needed for allnoconfig builds */

struct menu rootmenu;
static struct menu **last_entry_ptr;

struct file *file_list;
struct file *current_file;

void _menu_init(void)
{
	current_entry = current_menu = &rootmenu;
	last_entry_ptr = &rootmenu.list;
}

void menu_add_entry(struct symbol *sym)
{
	struct menu *menu;

	menu = xmalloc(sizeof(*menu));
	memset(menu, 0, sizeof(*menu));
	menu->sym = sym;
	menu->parent = current_menu;
	menu->file = current_file;
	menu->lineno = zconf_lineno();

	*last_entry_ptr = menu;
	last_entry_ptr = &menu->next;
	current_entry = menu;
	if (sym)
		menu_add_symbol(P_SYMBOL, sym, NULL);
}

struct menu *menu_add_menu(void)
{
	last_entry_ptr = &current_entry->list;
	current_menu = current_entry;
	return current_menu;
}

void menu_end_menu(void)
{
	last_entry_ptr = &current_menu->next;
	current_menu = current_menu->parent;
}

static struct expr *rewrite_m(struct expr *e)
{
	if (!e)
		return e;

	switch (e->type) {
	case E_NOT:
		e->left.expr = rewrite_m(e->left.expr);
		break;
	case E_OR:
	case E_AND:
		e->left.expr = rewrite_m(e->left.expr);
		e->right.expr = rewrite_m(e->right.expr);
		break;
	case E_SYMBOL:

		if (e->left.sym == &symbol_mod)
			return expr_alloc_and(e,
					      expr_alloc_symbol(modules_sym));
		break;
	default:
		break;
	}
	return e;
}

void menu_add_dep(struct expr *dep)
{
	current_entry->dep = expr_alloc_and(current_entry->dep, dep);
}

void menu_set_type(int type)
{
	struct symbol *sym = current_entry->sym;

	if (sym->type == type)
		return;
	if (sym->type == S_UNKNOWN) {
		sym->type = type;
		return;
	}
	/* silently ignore type redefinition */
}

static struct property *menu_add_prop(enum prop_type type, struct expr *expr,
				      struct expr *dep)
{
	struct property *prop;

	prop = xmalloc(sizeof(*prop));
	memset(prop, 0, sizeof(*prop));
	prop->type = type;
	prop->file = current_file;
	prop->lineno = zconf_lineno();
	prop->menu = current_entry;
	prop->expr = expr;
	prop->visible.expr = dep;

	if (current_entry->sym) {
		struct property **propp;

		for (propp = &current_entry->sym->prop; *propp;
		     propp = &(*propp)->next)
			;
		*propp = prop;
	}

	return prop;
}

struct property *menu_add_prompt(enum prop_type type, char *prompt,
				 struct expr *dep)
{
	struct property *prop = menu_add_prop(type, NULL, dep);

	if (isspace(*prompt)) {
		while (isspace(*prompt))
			prompt++;
	}

	if (type == P_PROMPT) {
		struct menu *menu = current_entry;

		while ((menu = menu->parent) != NULL) {
			struct expr *dup_expr;

			if (!menu->visibility)
				continue;

			dup_expr = expr_copy(menu->visibility);

			prop->visible.expr =
				expr_alloc_and(prop->visible.expr, dup_expr);
		}
	}

	current_entry->prompt = prop;
	prop->text = prompt;

	return prop;
}

void menu_add_expr(enum prop_type type, struct expr *expr, struct expr *dep)
{
	menu_add_prop(type, expr, dep);
}

void menu_add_symbol(enum prop_type type, struct symbol *sym, struct expr *dep)
{
	menu_add_prop(type, expr_alloc_symbol(sym), dep);
}

void menu_finalize(struct menu *parent)
{
	struct menu *menu, *last_menu;
	struct symbol *sym;
	struct property *prop;
	struct expr *parentdep, *basedep, *dep, *dep2, **ep;

	sym = parent->sym;
	if (parent->list) {
		if (sym && sym_is_choice(sym)) {
			if (sym->type == S_UNKNOWN) {
				current_entry = parent;
				for (menu = parent->list; menu;
				     menu = menu->next) {
					if (menu->sym &&
					    menu->sym->type != S_UNKNOWN) {
						menu_set_type(menu->sym->type);
						break;
					}
				}
			}

			for (menu = parent->list; menu; menu = menu->next) {
				current_entry = menu;
				if (menu->sym && menu->sym->type == S_UNKNOWN)
					menu_set_type(sym->type);
			}

			parentdep = expr_alloc_symbol(sym);
		} else {
			parentdep = parent->dep;
		}

		for (menu = parent->list; menu; menu = menu->next) {
			basedep = rewrite_m(menu->dep);
			basedep = expr_transform(basedep);
			basedep = expr_alloc_and(expr_copy(parentdep), basedep);
			basedep = expr_eliminate_dups(basedep);
			menu->dep = basedep;

			if (menu->sym)

				prop = menu->sym->prop;
			else

				prop = menu->prompt;

			for (; prop; prop = prop->next) {
				if (prop->menu != menu)

					continue;

				dep = rewrite_m(prop->visible.expr);
				dep = expr_transform(dep);
				dep = expr_alloc_and(expr_copy(basedep), dep);
				dep = expr_eliminate_dups(dep);
				if (menu->sym && menu->sym->type != S_TRISTATE)
					dep = expr_trans_bool(dep);
				prop->visible.expr = dep;

				if (prop->type == P_SELECT) {
					struct symbol *es =
						prop_get_symbol(prop);
					es->rev_dep.expr = expr_alloc_or(
						es->rev_dep.expr,
						expr_alloc_and(
							expr_alloc_symbol(
								menu->sym),
							expr_copy(dep)));
				}
			}
		}

		if (sym && sym_is_choice(sym))
			expr_free(parentdep);

		for (menu = parent->list; menu; menu = menu->next)
			menu_finalize(menu);
	} else if (sym) {
		basedep = parent->prompt ? parent->prompt->visible.expr : NULL;
		basedep = expr_trans_compare(basedep, E_UNEQUAL, &symbol_no);
		basedep = expr_eliminate_dups(expr_transform(basedep));

		last_menu = NULL;
		for (menu = parent->next; menu; menu = menu->next) {
			dep = menu->prompt ? menu->prompt->visible.expr :
					     menu->dep;
			if (!expr_contains_symbol(dep, sym))

				break;
			if (expr_depends_symbol(dep, sym))

				goto next;

			dep = expr_trans_compare(dep, E_UNEQUAL, &symbol_no);
			dep = expr_eliminate_dups(expr_transform(dep));
			dep2 = expr_copy(basedep);
			expr_eliminate_eq(&dep, &dep2);
			expr_free(dep);
			if (!expr_is_yes(dep2)) {
				expr_free(dep2);
				break;
			}

			expr_free(dep2);
next:
			menu_finalize(menu);
			menu->parent = parent;
			last_menu = menu;
		}
		expr_free(basedep);
		if (last_menu) {
			parent->list = parent->next;
			parent->next = last_menu->next;
			last_menu->next = NULL;
		}

		sym->dir_dep.expr =
			expr_alloc_or(sym->dir_dep.expr, parent->dep);
	}
	for (menu = parent->list; menu; menu = menu->next) {
		if (sym && sym_is_choice(sym) && menu->sym &&
		    !sym_is_choice_value(menu->sym)) {
			current_entry = menu;
			menu->sym->flags |= SYMBOL_CHOICEVAL;
			for (prop = menu->sym->prop; prop; prop = prop->next) {
				if (prop->menu == menu)
					continue;
			}

			if (sym->type == S_TRISTATE &&
			    menu->sym->type != S_TRISTATE) {
				basedep = expr_alloc_comp(E_EQUAL, sym,
							  &symbol_yes);
				menu->dep = expr_alloc_and(basedep, menu->dep);
				for (prop = menu->sym->prop; prop;
				     prop = prop->next) {
					if (prop->menu != menu)
						continue;
					prop->visible.expr = expr_alloc_and(
						expr_copy(basedep),
						prop->visible.expr);
				}
			}
			menu_add_symbol(P_CHOICE, sym, NULL);
			prop = sym_get_choice_prop(sym);
			for (ep = &prop->expr; *ep; ep = &(*ep)->left.expr)
				;
			*ep = expr_alloc_one(E_LIST, NULL);
			(*ep)->right.sym = menu->sym;
		}

		if (menu->list && (!menu->prompt || !menu->prompt->text)) {
			for (last_menu = menu->list;;
			     last_menu = last_menu->next) {
				last_menu->parent = parent;
				if (!last_menu->next)
					break;
			}
			last_menu->next = menu->next;
			menu->next = menu->list;
			menu->list = NULL;
		}
	}

	if (sym && !(sym->flags & SYMBOL_WARNED))
		sym->flags |= SYMBOL_WARNED;

	if (sym && !sym_is_optional(sym) && parent->prompt) {
		sym->rev_dep.expr = expr_alloc_or(
			sym->rev_dep.expr,
			expr_alloc_and(parent->prompt->visible.expr,
				       expr_alloc_symbol(&symbol_mod)));
	}
}

bool menu_has_prompt(struct menu *menu)
{
	if (!menu->prompt)
		return false;
	return true;
}

bool menu_is_visible(struct menu *menu)
{
	struct menu *child;
	struct symbol *sym;
	tristate visible;

	if (!menu->prompt)
		return false;

	if (menu->visibility) {
		if (expr_calc_value(menu->visibility) == no)
			return false;
	}

	sym = menu->sym;
	if (sym) {
		sym_calc_value(sym);
		visible = menu->prompt->visible.tri;
	} else
		visible = menu->prompt->visible.tri =
			expr_calc_value(menu->prompt->visible.expr);

	if (visible != no)
		return true;

	if (!sym || sym_get_tristate_value(menu->sym) == no)
		return false;

	for (child = menu->list; child; child = child->next) {
		if (menu_is_visible(child)) {
			if (sym)
				sym->flags |= SYMBOL_DEF_USER;
			return true;
		}
	}

	return false;
}

const char *menu_get_prompt(struct menu *menu)
{
	if (menu->prompt)
		return menu->prompt->text;
	else if (menu->sym)
		return menu->sym->name;
	return NULL;
}

/* menu_get_root_menu, menu_get_parent_menu, menu_has_help, menu_get_help,
   get_def_str, get_dep_str, get_prompt_str, get_symbol_props_str,
   get_symbol_str, get_relations_str, menu_get_ext_help removed -
   only used by interactive/help modes which were removed */
