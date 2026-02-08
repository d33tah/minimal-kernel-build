
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lkc.h"

/* expr_eliminate_yn forward decl removed - function removed */

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

enum string_value_kind {
	k_string,
	k_signed,
	k_unsigned,
};

union string_value {
	unsigned long long u;
	signed long long s;
};

static enum string_value_kind expr_parse_string(const char *str,
						enum symbol_type type,
						union string_value *val)
{
	char *tail;
	enum string_value_kind kind;

	errno = 0;
	switch (type) {
	case S_BOOLEAN:
	case S_TRISTATE:
		val->s = !strcmp(str, "n") ? 0 :
			 !strcmp(str, "m") ? 1 :
			 !strcmp(str, "y") ? 2 :
					     -1;
		return k_signed;
	case S_INT:
		val->s = strtoll(str, &tail, 10);
		kind = k_signed;
		break;
	case S_HEX:
		val->u = strtoull(str, &tail, 16);
		kind = k_unsigned;
		break;
	default:
		val->s = strtoll(str, &tail, 0);
		kind = k_signed;
		break;
	}
	return !errno && !*tail && tail > str && isxdigit(tail[-1]) ? kind :
								      k_string;
}

tristate expr_calc_value(struct expr *e)
{
	tristate val1, val2;
	const char *str1, *str2;
	enum string_value_kind k1 = k_string, k2 = k_string;
	union string_value lval = {}, rval = {};
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
	case E_GEQ:
	case E_GTH:
	case E_LEQ:
	case E_LTH:
	case E_UNEQUAL:
		break;
	default:
		printf("expr_calc_value: %d?\n", e->type);
		return no;
	}

	sym_calc_value(e->left.sym);
	sym_calc_value(e->right.sym);
	str1 = sym_get_string_value(e->left.sym);
	str2 = sym_get_string_value(e->right.sym);

	if (e->left.sym->type != S_STRING || e->right.sym->type != S_STRING) {
		k1 = expr_parse_string(str1, e->left.sym->type, &lval);
		k2 = expr_parse_string(str2, e->right.sym->type, &rval);
	}

	if (k1 == k_string || k2 == k_string)
		res = strcmp(str1, str2);
	else if (k1 == k_unsigned || k2 == k_unsigned)
		res = (lval.u > rval.u) - (lval.u < rval.u);
	else
		res = (lval.s > rval.s) - (lval.s < rval.s);

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
		printf("expr_calc_value: relation %d?\n", e->type);
		return no;
	}
}

static int expr_compare_type(enum expr_type t1, enum expr_type t2)
{
	if (t1 == t2)
		return 0;
	switch (t1) {
	case E_LEQ:
	case E_LTH:
	case E_GEQ:
	case E_GTH:
		if (t2 == E_EQUAL || t2 == E_UNEQUAL)
			return 1;
	case E_EQUAL:
	case E_UNEQUAL:
		if (t2 == E_NOT)
			return 1;
	case E_NOT:
		if (t2 == E_AND)
			return 1;
	case E_AND:
		if (t2 == E_OR)
			return 1;
	case E_OR:
		if (t2 == E_LIST)
			return 1;
	case E_LIST:
		if (t2 == 0)
			return 1;
	default:
		return -1;
	}
	printf("[%dgt%d?]", t1, t2);
	return 0;
}

void expr_print(struct expr *e,
		void (*fn)(void *, struct symbol *, const char *), void *data,
		int prevtoken)
{
	if (!e) {
		fn(data, NULL, "y");
		return;
	}

	if (expr_compare_type(prevtoken, e->type) > 0)
		fn(data, NULL, "(");
	switch (e->type) {
	case E_SYMBOL:
		if (e->left.sym->name)
			fn(data, e->left.sym, e->left.sym->name);
		else
			fn(data, NULL, "<choice>");
		break;
	case E_NOT:
		fn(data, NULL, "!");
		expr_print(e->left.expr, fn, data, E_NOT);
		break;
	case E_EQUAL:
		if (e->left.sym->name)
			fn(data, e->left.sym, e->left.sym->name);
		else
			fn(data, NULL, "<choice>");
		fn(data, NULL, "=");
		fn(data, e->right.sym, e->right.sym->name);
		break;
	case E_LEQ:
	case E_LTH:
		if (e->left.sym->name)
			fn(data, e->left.sym, e->left.sym->name);
		else
			fn(data, NULL, "<choice>");
		fn(data, NULL, e->type == E_LEQ ? "<=" : "<");
		fn(data, e->right.sym, e->right.sym->name);
		break;
	case E_GEQ:
	case E_GTH:
		if (e->left.sym->name)
			fn(data, e->left.sym, e->left.sym->name);
		else
			fn(data, NULL, "<choice>");
		fn(data, NULL, e->type == E_GEQ ? ">=" : ">");
		fn(data, e->right.sym, e->right.sym->name);
		break;
	case E_UNEQUAL:
		if (e->left.sym->name)
			fn(data, e->left.sym, e->left.sym->name);
		else
			fn(data, NULL, "<choice>");
		fn(data, NULL, "!=");
		fn(data, e->right.sym, e->right.sym->name);
		break;
	case E_OR:
		expr_print(e->left.expr, fn, data, E_OR);
		fn(data, NULL, " || ");
		expr_print(e->right.expr, fn, data, E_OR);
		break;
	case E_AND:
		expr_print(e->left.expr, fn, data, E_AND);
		fn(data, NULL, " && ");
		expr_print(e->right.expr, fn, data, E_AND);
		break;
	case E_LIST:
		fn(data, e->right.sym, e->right.sym->name);
		if (e->left.expr) {
			fn(data, NULL, " ^ ");
			expr_print(e->left.expr, fn, data, E_LIST);
		}
		break;
	case E_RANGE:
		fn(data, NULL, "[");
		fn(data, e->left.sym, e->left.sym->name);
		fn(data, NULL, " ");
		fn(data, e->right.sym, e->right.sym->name);
		fn(data, NULL, "]");
		break;
	default: {
		char buf[32];
		sprintf(buf, "<unknown type %d>", e->type);
		fn(data, NULL, buf);
		break;
	}
	}
	if (expr_compare_type(prevtoken, e->type) > 0)
		fn(data, NULL, ")");
}

/* expr_fprint, expr_print_file_helper removed - only used by DEBUG_EXPR */

static void expr_print_gstr_helper(void *data, struct symbol *sym,
				   const char *str)
{
	struct gstr *gs = (struct gstr *)data;
	const char *sym_str = NULL;

	if (sym)
		sym_str = sym_get_string_value(sym);

	if (gs->max_width) {
		unsigned extra_length = strlen(str);
		const char *last_cr = strrchr(gs->s, '\n');
		unsigned last_line_length;

		if (sym_str)
			extra_length += 4 + strlen(sym_str);

		if (!last_cr)
			last_cr = gs->s;

		last_line_length = strlen(gs->s) - (last_cr - gs->s);

		if ((last_line_length + extra_length) > gs->max_width)
			str_append(gs, "\\\n");
	}

	str_append(gs, str);
	if (sym && sym->type != S_UNKNOWN)
		str_printf(gs, " [=%s]", sym_str);
}

void expr_gstr_print(struct expr *e, struct gstr *gs)
{
	expr_print(e, expr_print_gstr_helper, gs, E_NONE);
}

static void expr_print_revdep(struct expr *e,
			      void (*fn)(void *, struct symbol *, const char *),
			      void *data, tristate pr_type, const char **title)
{
	if (e->type == E_OR) {
		expr_print_revdep(e->left.expr, fn, data, pr_type, title);
		expr_print_revdep(e->right.expr, fn, data, pr_type, title);
	} else if (expr_calc_value(e) == pr_type) {
		if (*title) {
			fn(data, NULL, *title);
			*title = NULL;
		}

		fn(data, NULL, "  - ");
		expr_print(e, fn, data, E_NONE);
		fn(data, NULL, "\n");
	}
}

void expr_gstr_print_revdep(struct expr *e, struct gstr *gs, tristate pr_type,
			    const char *title)
{
	expr_print_revdep(e, expr_print_gstr_helper, gs, pr_type, &title);
}
