
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lkc.h"

struct expr *expr_alloc_symbol(struct symbol *sym)
{
	struct expr *e = xcalloc(1, sizeof(*e));
	e->type = E_SYMBOL;
	e->left.sym = sym;
	return e;
}

struct expr *expr_alloc_one(enum expr_type type, struct expr *ce)
{
	struct expr *e = xcalloc(1, sizeof(*e));
	e->type = type;
	e->left.expr = ce;
	return e;
}

struct expr *expr_alloc_two(enum expr_type type, struct expr *e1,
			    struct expr *e2)
{
	struct expr *e = xcalloc(1, sizeof(*e));
	e->type = type;
	e->left.expr = e1;
	e->right.expr = e2;
	return e;
}

struct expr *expr_alloc_comp(enum expr_type type, struct symbol *s1,
			     struct symbol *s2)
{
	struct expr *e = xcalloc(1, sizeof(*e));
	e->type = type;
	e->left.sym = s1;
	e->right.sym = s2;
	return e;
}

struct expr *expr_alloc_and(struct expr *e1, struct expr *e2)
{
	if (!e1)
		return e2;
	return e2 ? expr_alloc_two(E_AND, e1, e2) : e1;
}

struct expr *expr_alloc_or(struct expr *e1, struct expr *e2)
{
	if (!e1)
		return e2;
	return e2 ? expr_alloc_two(E_OR, e1, e2) : e1;
}

struct expr *expr_copy(const struct expr *org)
{
	struct expr *e;

	if (!org)
		return NULL;

	e = xmalloc(sizeof(*org));
	memcpy(e, org, sizeof(*org));
	switch (org->type) {
	case E_SYMBOL:
		e->left = org->left;
		break;
	case E_NOT:
		e->left.expr = expr_copy(org->left.expr);
		break;
	case E_EQUAL:
	case E_GEQ:
	case E_GTH:
	case E_LEQ:
	case E_LTH:
	case E_UNEQUAL:
		e->left.sym = org->left.sym;
		e->right.sym = org->right.sym;
		break;
	case E_AND:
	case E_OR:
	case E_LIST:
		e->left.expr = expr_copy(org->left.expr);
		e->right.expr = expr_copy(org->right.expr);
		break;
	default:
		fprintf(stderr, "can't copy type %d\n", e->type);
		free(e);
		e = NULL;
		break;
	}

	return e;
}

void expr_free(struct expr *e)
{
	if (!e)
		return;

	switch (e->type) {
	case E_SYMBOL:
		break;
	case E_NOT:
		expr_free(e->left.expr);
		break;
	case E_EQUAL:
	case E_GEQ:
	case E_GTH:
	case E_LEQ:
	case E_LTH:
	case E_UNEQUAL:
		break;
	case E_OR:
	case E_AND:
		expr_free(e->left.expr);
		expr_free(e->right.expr);
		break;
	default:
		fprintf(stderr, "how to free type %d?\n", e->type);
		break;
	}
	free(e);
}

/* expr_eliminate_eq, __expr_eliminate_eq, expr_eq, expr_eliminate_yn removed -
   expression optimization/simplification not needed for minimal tinyconfig */
void expr_eliminate_eq(struct expr **ep1, struct expr **ep2)
{
}

struct expr *expr_trans_bool(struct expr *e)
{
	if (!e)
		return NULL;
	switch (e->type) {
	case E_AND:
	case E_OR:
	case E_NOT:
		e->left.expr = expr_trans_bool(e->left.expr);
		e->right.expr = expr_trans_bool(e->right.expr);
		break;
	case E_UNEQUAL:

		if (e->left.sym->type == S_TRISTATE) {
			if (e->right.sym == &symbol_no) {
				e->type = E_SYMBOL;
				e->right.sym = NULL;
			}
		}
		break;
	default:;
	}
	return e;
}

/* expr_join_or, expr_join_and, expr_eliminate_dups1 removed -
   expression deduplication/optimization not needed for minimal build */
struct expr *expr_eliminate_dups(struct expr *e)
{
	return e;
}

struct expr *expr_transform(struct expr *e)
{
	struct expr *tmp;

	if (!e)
		return NULL;
	switch (e->type) {
	case E_EQUAL:
	case E_GEQ:
	case E_GTH:
	case E_LEQ:
	case E_LTH:
	case E_UNEQUAL:
	case E_SYMBOL:
	case E_LIST:
		break;
	default:
		e->left.expr = expr_transform(e->left.expr);
		e->right.expr = expr_transform(e->right.expr);
	}

	switch (e->type) {
	case E_EQUAL:
		if (e->left.sym->type != S_BOOLEAN)
			break;
		if (e->right.sym == &symbol_no) {
			e->type = E_NOT;
			e->left.expr = expr_alloc_symbol(e->left.sym);
			e->right.sym = NULL;
			break;
		}
		if (e->right.sym == &symbol_mod) {
			printf("boolean symbol %s tested for 'm'? test forced to 'n'\n",
			       e->left.sym->name);
			e->type = E_SYMBOL;
			e->left.sym = &symbol_no;
			e->right.sym = NULL;
			break;
		}
		if (e->right.sym == &symbol_yes) {
			e->type = E_SYMBOL;
			e->right.sym = NULL;
			break;
		}
		break;
	case E_UNEQUAL:
		if (e->left.sym->type != S_BOOLEAN)
			break;
		if (e->right.sym == &symbol_no) {
			e->type = E_SYMBOL;
			e->right.sym = NULL;
			break;
		}
		if (e->right.sym == &symbol_mod) {
			printf("boolean symbol %s tested for 'm'? test forced to 'y'\n",
			       e->left.sym->name);
			e->type = E_SYMBOL;
			e->left.sym = &symbol_yes;
			e->right.sym = NULL;
			break;
		}
		if (e->right.sym == &symbol_yes) {
			e->type = E_NOT;
			e->left.expr = expr_alloc_symbol(e->left.sym);
			e->right.sym = NULL;
			break;
		}
		break;
	case E_NOT:
		switch (e->left.expr->type) {
		case E_NOT:

			tmp = e->left.expr->left.expr;
			free(e->left.expr);
			free(e);
			e = tmp;
			e = expr_transform(e);
			break;
		case E_EQUAL:
		case E_UNEQUAL:

			tmp = e->left.expr;
			free(e);
			e = tmp;
			e->type = e->type == E_EQUAL ? E_UNEQUAL : E_EQUAL;
			break;
		case E_LEQ:
		case E_GEQ:
			tmp = e->left.expr;
			free(e);
			e = tmp;
			e->type = e->type == E_LEQ ? E_GTH : E_LTH;
			break;
		case E_LTH:
		case E_GTH:
			tmp = e->left.expr;
			free(e);
			e = tmp;
			e->type = e->type == E_LTH ? E_GEQ : E_LEQ;
			break;
		case E_OR:

			tmp = e->left.expr;
			e->type = E_AND;
			e->right.expr = expr_alloc_one(E_NOT, tmp->right.expr);
			tmp->type = E_NOT;
			tmp->right.expr = NULL;
			e = expr_transform(e);
			break;
		case E_AND:

			tmp = e->left.expr;
			e->type = E_OR;
			e->right.expr = expr_alloc_one(E_NOT, tmp->right.expr);
			tmp->type = E_NOT;
			tmp->right.expr = NULL;
			e = expr_transform(e);
			break;
		case E_SYMBOL:
			if (e->left.expr->left.sym == &symbol_yes) {
				tmp = e->left.expr;
				free(e);
				e = tmp;
				e->type = E_SYMBOL;
				e->left.sym = &symbol_no;
				break;
			}
			if (e->left.expr->left.sym == &symbol_mod) {
				tmp = e->left.expr;
				free(e);
				e = tmp;
				e->type = E_SYMBOL;
				e->left.sym = &symbol_mod;
				break;
			}
			if (e->left.expr->left.sym == &symbol_no) {
				tmp = e->left.expr;
				free(e);
				e = tmp;
				e->type = E_SYMBOL;
				e->left.sym = &symbol_yes;
				break;
			}
			break;
		default:;
		}
		break;
	default:;
	}
	return e;
}

int expr_contains_symbol(struct expr *dep, struct symbol *sym)
{
	if (!dep)
		return 0;

	switch (dep->type) {
	case E_AND:
	case E_OR:
		return expr_contains_symbol(dep->left.expr, sym) ||
		       expr_contains_symbol(dep->right.expr, sym);
	case E_SYMBOL:
		return dep->left.sym == sym;
	case E_EQUAL:
	case E_GEQ:
	case E_GTH:
	case E_LEQ:
	case E_LTH:
	case E_UNEQUAL:
		return dep->left.sym == sym || dep->right.sym == sym;
	case E_NOT:
		return expr_contains_symbol(dep->left.expr, sym);
	default:;
	}
	return 0;
}

bool expr_depends_symbol(struct expr *dep, struct symbol *sym)
{
	if (!dep)
		return false;

	switch (dep->type) {
	case E_AND:
		return expr_depends_symbol(dep->left.expr, sym) ||
		       expr_depends_symbol(dep->right.expr, sym);
	case E_SYMBOL:
		return dep->left.sym == sym;
	case E_EQUAL:
		if (dep->left.sym == sym) {
			if (dep->right.sym == &symbol_yes ||
			    dep->right.sym == &symbol_mod)
				return true;
		}
		break;
	case E_UNEQUAL:
		if (dep->left.sym == sym) {
			if (dep->right.sym == &symbol_no)
				return true;
		}
		break;
	default:;
	}
	return false;
}

struct expr *expr_trans_compare(struct expr *e, enum expr_type type,
				struct symbol *sym)
{
	struct expr *e1, *e2;

	if (!e) {
		e = expr_alloc_symbol(sym);
		if (type == E_UNEQUAL)
			e = expr_alloc_one(E_NOT, e);
		return e;
	}
	switch (e->type) {
	case E_AND:
		e1 = expr_trans_compare(e->left.expr, E_EQUAL, sym);
		e2 = expr_trans_compare(e->right.expr, E_EQUAL, sym);
		if (sym == &symbol_yes)
			e = expr_alloc_two(E_AND, e1, e2);
		if (sym == &symbol_no)
			e = expr_alloc_two(E_OR, e1, e2);
		if (type == E_UNEQUAL)
			e = expr_alloc_one(E_NOT, e);
		return e;
	case E_OR:
		e1 = expr_trans_compare(e->left.expr, E_EQUAL, sym);
		e2 = expr_trans_compare(e->right.expr, E_EQUAL, sym);
		if (sym == &symbol_yes)
			e = expr_alloc_two(E_OR, e1, e2);
		if (sym == &symbol_no)
			e = expr_alloc_two(E_AND, e1, e2);
		if (type == E_UNEQUAL)
			e = expr_alloc_one(E_NOT, e);
		return e;
	case E_NOT:
		return expr_trans_compare(e->left.expr,
					  type == E_EQUAL ? E_UNEQUAL : E_EQUAL,
					  sym);
	case E_UNEQUAL:
	case E_LTH:
	case E_LEQ:
	case E_GTH:
	case E_GEQ:
	case E_EQUAL:
		if (type == E_EQUAL) {
			if (sym == &symbol_yes)
				return expr_copy(e);
			if (sym == &symbol_mod)
				return expr_alloc_symbol(&symbol_no);
			if (sym == &symbol_no)
				return expr_alloc_one(E_NOT, expr_copy(e));
		} else {
			if (sym == &symbol_yes)
				return expr_alloc_one(E_NOT, expr_copy(e));
			if (sym == &symbol_mod)
				return expr_alloc_symbol(&symbol_yes);
			if (sym == &symbol_no)
				return expr_copy(e);
		}
		break;
	case E_SYMBOL:
		return expr_alloc_comp(type, e->left.sym, sym);
	case E_LIST:
	case E_RANGE:
	case E_NONE:;
	}
	return NULL;
}

tristate expr_calc_value(struct expr *e)
{
	tristate val1, val2;
	const char *str1, *str2;
	int res;

	if (!e)
		return yes;

	switch (e->type) {
	case E_SYMBOL:
		sym_calc_value(e->left.sym);
		return e->left.sym->curr.tri;
	case E_AND:
		val1 = expr_calc_value(e->left.expr);
		val2 = expr_calc_value(e->right.expr);
		return EXPR_AND(val1, val2);
	case E_OR:
		val1 = expr_calc_value(e->left.expr);
		val2 = expr_calc_value(e->right.expr);
		return EXPR_OR(val1, val2);
	case E_NOT:
		val1 = expr_calc_value(e->left.expr);
		return EXPR_NOT(val1);
	case E_EQUAL:
	case E_UNEQUAL:
	case E_LTH:
	case E_LEQ:
	case E_GTH:
	case E_GEQ:
		break;
	default:
		return no;
	}

	sym_calc_value(e->left.sym);
	sym_calc_value(e->right.sym);
	str1 = sym_get_string_value(e->left.sym);
	str2 = sym_get_string_value(e->right.sym);
	res = strcmp(str1, str2);

	switch (e->type) {
	case E_EQUAL:
		return res ? no : yes;
	case E_GEQ:
		return res >= 0 ? yes : no;
	case E_GTH:
		return res > 0 ? yes : no;
	case E_LEQ:
		return res <= 0 ? yes : no;
	case E_LTH:
		return res < 0 ? yes : no;
	case E_UNEQUAL:
		return res ? yes : no;
	default:
		return no;
	}
}

/* expr_compare_type, expr_print, expr_print_gstr_helper,
   expr_gstr_print, expr_print_revdep, expr_gstr_print_revdep removed -
   sym_warn_unmet_dep simplified to not need expression printing (~190 LOC) */
