// SPDX-License-Identifier: GPL-2.0
// Hand-written Kconfig lexer + recursive-descent parser.
// Replaces flex-generated lexer.lex.c and bison-generated parser.tab.c/h.

#include <ctype.h>
#include <limits.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lkc.h"
extern struct menu *current_menu, *current_entry;

enum {
	T_HELPTEXT = 258,
	T_WORD,
	T_WORD_QUOTE,
	T_BOOL,
	T_CHOICE,
	T_CLOSE_PAREN,
	T_COLON_EQUAL,
	T_COMMENT,
	T_CONFIG,
	T_DEFAULT,
	T_DEF_BOOL,
	T_DEPENDS,
	T_ENDCHOICE,
	T_ENDIF,
	T_ENDMENU,
	T_HELP,
	T_HEX,
	T_IF,
	T_INT,
	T_MAINMENU,
	T_MENU,
	T_MENUCONFIG,
	T_MODULES,
	T_ON,
	T_OPEN_PAREN,
	T_PLUS_EQUAL,
	T_PROMPT,
	T_RANGE,
	T_SELECT,
	T_SOURCE,
	T_STRING,
	T_TRISTATE,
	T_EOL,
	T_OR,
	T_AND,
	T_EQUAL,
	T_UNEQUAL,
	T_LESS,
	T_LESS_EQUAL,
	T_GREATER,
	T_GREATER_EQUAL,
	T_NOT,
	T_EOF
};

int yylineno;
struct symbol *symbol_hash[SYMBOL_HASHSIZE];
struct menu *current_menu, *current_entry;

struct input_file {
	struct input_file *parent;
	FILE *fp;
	struct file *file;
	int lineno;
};

static struct input_file *cur_input;
static struct {
	struct file *file;
	int lineno;
} current_pos;
static int cur_tok;
static char *cur_string;
static int yynerrs;
static void parse_stmt_list(bool in_choice);
static struct expr *parse_expr(void);

static void zconf_error(const char *err, ...)
{
	va_list ap;
	yynerrs++;
	fprintf(stderr, "%s:%d: ", zconf_curname(), zconf_lineno());
	va_start(ap, err);
	vfprintf(stderr, err, ap);
	va_end(ap);
	fprintf(stderr, "\n");
}

FILE *zconf_fopen(const char *name)
{
	char fullname[PATH_MAX + 1];
	FILE *f = fopen(name, "r");
	if (!f && name && name[0] != '/') {
		char *env = getenv(SRCTREE);
		if (env) {
			snprintf(fullname, sizeof(fullname), "%s/%s", env,
				 name);
			f = fopen(fullname, "r");
		}
	}
	return f;
}

int zconf_lineno(void)
{
	return current_pos.lineno;
}
const char *zconf_curname(void)
{
	return current_pos.file ? current_pos.file->name : "<none>";
}

void zconf_initscan(const char *name)
{
	FILE *f = zconf_fopen(name);
	if (!f) {
		fprintf(stderr, "can't find file %s\n", name);
		exit(1);
	}
	cur_input = xcalloc(1, sizeof(*cur_input));
	cur_input->fp = f;
	cur_input->file = file_lookup(name);
	cur_input->lineno = 1;
	current_file = cur_input->file;
	yylineno = 1;
}

void zconf_nextfile(const char *name)
{
	struct file *iter, *file = file_lookup(name);
	FILE *f = zconf_fopen(file->name);
	struct input_file *inp;
	if (!f) {
		fprintf(stderr, "%s:%d: can't open file \"%s\"\n",
			zconf_curname(), zconf_lineno(), file->name);
		exit(1);
	}
	cur_input->lineno = yylineno;
	current_file->lineno = yylineno;
	file->parent = current_file;
	for (iter = current_file; iter; iter = iter->parent)
		if (!strcmp(iter->name, file->name)) {
			fprintf(stderr,
				"Recursive inclusion detected.\n"
				"Inclusion path:\n  current file : %s\n",
				file->name);
			iter = file;
			do {
				iter = iter->parent;
				fprintf(stderr, "  included from: %s:%d\n",
					iter->name, iter->lineno - 1);
			} while (strcmp(iter->name, file->name));
			exit(1);
		}
	inp = xcalloc(1, sizeof(*inp));
	inp->fp = f;
	inp->file = file;
	inp->lineno = 1;
	inp->parent = cur_input;
	cur_input = inp;
	current_file = file;
	yylineno = 1;
}

static bool zconf_endfile(void)
{
	struct input_file *parent = cur_input->parent;
	fclose(cur_input->fp);
	free(cur_input);
	cur_input = parent;
	if (!cur_input)
		return false;
	current_file = cur_input->file;
	yylineno = cur_input->lineno;
	return true;
}

static int lexchar(void)
{
	int c;
	for (;;) {
		if (!cur_input)
			return EOF;
		c = fgetc(cur_input->fp);
		if (c == '\n')
			yylineno++;
		if (c != EOF)
			return c;
		if (!zconf_endfile())
			return EOF;
	}
}

static void unlexchar(int c)
{
	if (c == EOF)
		return;
	if (c == '\n')
		yylineno--;
	ungetc(c, cur_input->fp);
}

static char *strbuf;
static int strbuf_len, strbuf_alloc;

static void strbuf_reset(void)
{
	strbuf_len = 0;
	if (!strbuf) {
		strbuf_alloc = 256;
		strbuf = xmalloc(strbuf_alloc);
	}
	strbuf[0] = 0;
}

static void strbuf_addch(int c)
{
	if (strbuf_len + 2 > strbuf_alloc) {
		strbuf_alloc *= 2;
		strbuf = xrealloc(strbuf, strbuf_alloc);
	}
	strbuf[strbuf_len++] = c;
	strbuf[strbuf_len] = 0;
}

static char *strbuf_detach(void)
{
	return xstrdup(strbuf);
}

static const struct {
	const char *name;
	int token;
} keywords[] = {
	{ "bool", T_BOOL },
	{ "choice", T_CHOICE },
	{ "comment", T_COMMENT },
	{ "config", T_CONFIG },
	{ "def_bool", T_DEF_BOOL },
	{ "default", T_DEFAULT },
	{ "depends", T_DEPENDS },
	{ "endchoice", T_ENDCHOICE },
	{ "endif", T_ENDIF },
	{ "endmenu", T_ENDMENU },
	{ "help", T_HELP },
	{ "hex", T_HEX },
	{ "if", T_IF },
	{ "int", T_INT },
	{ "mainmenu", T_MAINMENU },
	{ "menu", T_MENU },
	{ "menuconfig", T_MENUCONFIG },
	{ "modules", T_MODULES },
	{ "on", T_ON },
	{ "prompt", T_PROMPT },
	{ "range", T_RANGE },
	{ "select", T_SELECT },
	{ "source", T_SOURCE },
	{ "string", T_STRING },
	{ "tristate", T_TRISTATE },
};

static int lookup_keyword(const char *s)
{
	unsigned i;
	for (i = 0; i < sizeof(keywords) / sizeof(keywords[0]); i++)
		if (!strcmp(s, keywords[i].name))
			return keywords[i].token;
	return 0;
}

static char *lex_quoted_string(int quote)
{
	int c;
	strbuf_reset();
	for (;;) {
		c = lexchar();
		if (c == EOF || c == '\n') {
			if (c == '\n')
				unlexchar(c);
			break;
		}
		if (c == quote)
			break;
		if (c == '\\') {
			c = lexchar();
			if (c == EOF)
				break;
			if (c == '\n')
				continue;
			strbuf_addch(c);
			continue;
		}
		if (c == '$') {
			char tmp[4096];
			int tl = 0, i, used;
			const char *p;
			char *exp;
			for (;;) {
				int c2 = lexchar();
				if (c2 == EOF || c2 == '\n') {
					if (c2 == '\n')
						unlexchar(c2);
					break;
				}
				if (tl < (int)sizeof(tmp) - 1)
					tmp[tl++] = c2;
			}
			tmp[tl] = 0;
			p = tmp;
			exp = expand_dollar(&p);
			used = p - tmp;
			for (i = tl - 1; i >= used; i--)
				unlexchar(tmp[i]);
			for (i = 0; exp[i]; i++)
				strbuf_addch(exp[i]);
			free(exp);
			continue;
		}
		strbuf_addch(c);
	}
	return strbuf_detach();
}

static char *lex_raw_line(void)
{
	int c;
	for (;;) {
		c = lexchar();
		if (c == '\n') {
			unlexchar(c);
			return xstrdup("");
		}
		if (c == EOF)
			return xstrdup("");
		if (c != ' ' && c != '\t')
			break;
	}
	strbuf_reset();
	strbuf_addch(c);
	for (;;) {
		c = lexchar();
		if (c == '\n') {
			unlexchar(c);
			break;
		}
		if (c == EOF)
			break;
		strbuf_addch(c);
	}
	return strbuf_detach();
}

static char *lex_helptext(void)
{
	int first_ts = 0, c, ts, rel, i;
	strbuf_reset();
	for (;;) {
		c = lexchar();
		if (c == EOF)
			break;
		ts = 0;
		while (c == ' ' || c == '\t') {
			if (c == '\t')
				ts = (ts & ~7) + 8;
			else
				ts++;
			c = lexchar();
		}
		if (c == '\n') {
			strbuf_addch('\n');
			continue;
		}
		if (c == EOF)
			break;
		if (first_ts == 0)
			first_ts = ts;
		if (ts < first_ts) {
			unlexchar(c);
			break;
		}
		rel = ts - first_ts;
		for (i = 0; i < rel; i++)
			strbuf_addch(' ');
		while (c != '\n' && c != EOF) {
			strbuf_addch(c);
			c = lexchar();
		}
		while (strbuf_len > 0 && (strbuf[strbuf_len - 1] == ' ' ||
					  strbuf[strbuf_len - 1] == '\t'))
			strbuf[--strbuf_len] = 0;
		strbuf_addch('\n');
	}
	return strbuf_detach();
}

static int next_token(void)
{
	int c, c2, kw;
	if (cur_string) {
		free(cur_string);
		cur_string = NULL;
	}
skip:
	c = lexchar();
	if (c == EOF)
		return T_EOF;
	if (c == ' ' || c == '\t')
		goto skip;
	if (c == '#') {
		while ((c = lexchar()) != '\n' && c != EOF)
			;
		if (c == '\n')
			unlexchar(c);
		goto skip;
	}
	if (c == '\\') {
		c2 = lexchar();
		if (c2 == '\n')
			goto skip;
		unlexchar(c2);
	}
	if (c == '\n')
		return T_EOL;
	if (c == '|') {
		c2 = lexchar();
		if (c2 == '|')
			return T_OR;
		unlexchar(c2);
		goto skip;
	}
	if (c == '&') {
		c2 = lexchar();
		if (c2 == '&')
			return T_AND;
		unlexchar(c2);
		goto skip;
	}
	if (c == '!') {
		c2 = lexchar();
		if (c2 == '=')
			return T_UNEQUAL;
		unlexchar(c2);
		return T_NOT;
	}
	if (c == '<') {
		c2 = lexchar();
		if (c2 == '=')
			return T_LESS_EQUAL;
		unlexchar(c2);
		return T_LESS;
	}
	if (c == '>') {
		c2 = lexchar();
		if (c2 == '=')
			return T_GREATER_EQUAL;
		unlexchar(c2);
		return T_GREATER;
	}
	if (c == ':') {
		c2 = lexchar();
		if (c2 == '=')
			return T_COLON_EQUAL;
		unlexchar(c2);
		goto skip;
	}
	if (c == '+') {
		c2 = lexchar();
		if (c2 == '=')
			return T_PLUS_EQUAL;
		unlexchar(c2);
		goto skip;
	}
	if (c == '=')
		return T_EQUAL;
	if (c == '(')
		return T_OPEN_PAREN;
	if (c == ')')
		return T_CLOSE_PAREN;
	if (c == '"' || c == '\'') {
		cur_string = lex_quoted_string(c);
		return T_WORD_QUOTE;
	}
	if (isalnum(c) || c == '_' || c == '-') {
		bool has_dollar = false;
		strbuf_reset();
		do {
			strbuf_addch(c);
			c = lexchar();
		} while (c != EOF && (isalnum(c) || c == '_' || c == '-'));
		if (c == '$')
			has_dollar = true;
		if (!has_dollar) {
			if (c != EOF)
				unlexchar(c);
			cur_string = strbuf_detach();
			kw = lookup_keyword(cur_string);
			if (kw) {
				free(cur_string);
				cur_string = NULL;
				return kw;
			}
			return T_WORD;
		}
		strbuf_addch(c);
		goto expand_dollar_token;
	}
	if (c == '$') {
		strbuf_reset();
		strbuf_addch(c);
expand_dollar_token:
		for (;;) {
			c = lexchar();
			if (c == EOF || c == '\n') {
				if (c != EOF)
					unlexchar(c);
				break;
			}
			strbuf_addch(c);
		}
		{
			const char *r = strbuf;
			int rl, i;
			cur_string = expand_one_token(&r);
			rl = strlen(r);
			for (i = rl - 1; i >= 0; i--)
				unlexchar(r[i]);
		}
		if (!cur_string[0]) {
			free(cur_string);
			cur_string = NULL;
			goto skip;
		}
		return T_WORD;
	}
	goto skip;
}

static int prev_token_type = T_EOL;
static void advance(void)
{
	int tok;
repeat:
	tok = next_token();
	if (prev_token_type == T_EOL || prev_token_type == T_HELPTEXT) {
		if (tok == T_EOL)
			goto repeat;
		current_pos.file = current_file;
		current_pos.lineno = yylineno;
	}
	prev_token_type = tok;
	cur_tok = tok;
}

static bool eat(int tok)
{
	if (cur_tok == tok) {
		advance();
		return true;
	}
	return false;
}
static void expect(int tok)
{
	if (!eat(tok))
		zconf_error("unexpected token (expected %d, got %d)", tok,
			    cur_tok);
}
static void skip_to_eol(void)
{
	while (cur_tok != T_EOL && cur_tok != T_EOF)
		advance();
	if (cur_tok == T_EOL)
		advance();
}
static char *eat_string(void)
{
	char *s = cur_string;
	cur_string = NULL;
	advance();
	return s;
}

static struct symbol *parse_symbol(void)
{
	char *s;
	struct symbol *sym;
	if (cur_tok == T_WORD) {
		s = eat_string();
		sym = sym_lookup(s, 0);
		free(s);
		return sym;
	}
	if (cur_tok == T_WORD_QUOTE) {
		s = eat_string();
		sym = sym_lookup(s, SYMBOL_CONST);
		free(s);
		return sym;
	}
	zconf_error("expected symbol");
	return &symbol_no;
}

static struct expr *parse_unary(void)
{
	struct expr *e;
	struct symbol *sym, *rhs;
	enum expr_type cmp;
	if (eat(T_NOT))
		return expr_alloc_one(E_NOT, parse_unary());
	if (eat(T_OPEN_PAREN)) {
		e = parse_expr();
		expect(T_CLOSE_PAREN);
		return e;
	}
	sym = parse_symbol();
	cmp = E_NONE;
	if (cur_tok == T_EQUAL)
		cmp = E_EQUAL;
	else if (cur_tok == T_UNEQUAL)
		cmp = E_UNEQUAL;
	else if (cur_tok == T_LESS)
		cmp = E_LTH;
	else if (cur_tok == T_LESS_EQUAL)
		cmp = E_LEQ;
	else if (cur_tok == T_GREATER)
		cmp = E_GTH;
	else if (cur_tok == T_GREATER_EQUAL)
		cmp = E_GEQ;
	if (cmp != E_NONE) {
		advance();
		rhs = parse_symbol();
		return expr_alloc_comp(cmp, sym, rhs);
	}
	return expr_alloc_symbol(sym);
}

static struct expr *parse_and_expr(void)
{
	struct expr *e = parse_unary();
	while (eat(T_AND))
		e = expr_alloc_two(E_AND, e, parse_unary());
	return e;
}

static struct expr *parse_expr(void)
{
	struct expr *e = parse_and_expr();
	while (eat(T_OR))
		e = expr_alloc_two(E_OR, e, parse_and_expr());
	return e;
}

static struct expr *parse_if_expr(void)
{
	return eat(T_IF) ? parse_expr() : NULL;
}

static void parse_help(void)
{
	if (cur_tok != T_HELP)
		return;
	advance();
	expect(T_EOL);
	free(lex_helptext());
	prev_token_type = T_HELPTEXT;
	advance();
}

static void parse_config_options(void)
{
	for (;;) {
		if (cur_tok == T_BOOL || cur_tok == T_TRISTATE ||
		    cur_tok == T_INT || cur_tok == T_HEX ||
		    cur_tok == T_STRING) {
			enum symbol_type st;
			switch (cur_tok) {
			case T_BOOL:
				st = S_BOOLEAN;
				break;
			case T_TRISTATE:
				st = S_TRISTATE;
				break;
			case T_INT:
				st = S_INT;
				break;
			case T_HEX:
				st = S_HEX;
				break;
			default:
				st = S_STRING;
				break;
			}
			advance();
			menu_set_type(st);
			if (cur_tok == T_WORD_QUOTE) {
				char *p = eat_string();
				menu_add_prompt(P_PROMPT, p, parse_if_expr());
			}
			expect(T_EOL);
		} else if (cur_tok == T_DEF_BOOL) {
			struct expr *v, *d;
			advance();
			menu_set_type(S_BOOLEAN);
			v = parse_expr();
			d = parse_if_expr();
			menu_add_expr(P_DEFAULT, v, d);
			expect(T_EOL);
		} else if (eat(T_PROMPT)) {
			char *p = eat_string();
			struct expr *d = parse_if_expr();
			menu_add_prompt(P_PROMPT, p, d);
			expect(T_EOL);
		} else if (eat(T_DEFAULT)) {
			struct expr *v = parse_expr(), *d = parse_if_expr();
			menu_add_expr(P_DEFAULT, v, d);
			expect(T_EOL);
		} else if (eat(T_DEPENDS)) {
			struct expr *d;
			expect(T_ON);
			d = parse_expr();
			menu_add_dep(d);
			expect(T_EOL);
		} else if (eat(T_SELECT)) {
			char *s = eat_string();
			struct symbol *sym = sym_lookup(s, 0);
			struct expr *d;
			free(s);
			d = parse_if_expr();
			menu_add_symbol(P_SELECT, sym, d);
			expect(T_EOL);
		} else if (eat(T_RANGE)) {
			parse_symbol();
			parse_symbol();
			parse_if_expr();
			expect(T_EOL);
		} else if (eat(T_MODULES)) {
			if (modules_sym)
				zconf_error(
					"symbol '%s' redefines 'modules' already defined by '%s'",
					current_entry->sym->name,
					modules_sym->name);
			modules_sym = current_entry->sym;
			expect(T_EOL);
		} else if (cur_tok == T_HELP) {
			parse_help();
		} else
			break;
	}
}

static void parse_config_stmt(void)
{
	char *n = eat_string();
	struct symbol *s = sym_lookup(n, 0);
	free(n);
	s->flags |= SYMBOL_OPTIONAL;
	menu_add_entry(s);
	expect(T_EOL);
	parse_config_options();
}

static void parse_menuconfig_stmt(void)
{
	char *n = eat_string();
	struct symbol *s = sym_lookup(n, 0);
	free(n);
	s->flags |= SYMBOL_OPTIONAL;
	menu_add_entry(s);
	expect(T_EOL);
	parse_config_options();
	if (current_entry->prompt)
		current_entry->prompt->type = P_MENU;
	else
		fprintf(stderr,
			"%s:%d: warning: menuconfig statement without prompt\n",
			zconf_curname(), zconf_lineno());
}

static void parse_choice_options(void)
{
	for (;;) {
		if (eat(T_PROMPT)) {
			char *p = eat_string();
			struct expr *d = parse_if_expr();
			menu_add_prompt(P_PROMPT, p, d);
			expect(T_EOL);
		} else if (cur_tok == T_BOOL || cur_tok == T_TRISTATE) {
			enum symbol_type st = (cur_tok == T_BOOL) ? S_BOOLEAN :
								    S_TRISTATE;
			advance();
			menu_set_type(st);
			if (cur_tok == T_WORD_QUOTE) {
				char *p = eat_string();
				menu_add_prompt(P_PROMPT, p, parse_if_expr());
			}
			expect(T_EOL);
		} else if (eat(T_DEFAULT)) {
			char *s = eat_string();
			struct symbol *sym = sym_lookup(s, 0);
			struct expr *d;
			free(s);
			d = parse_if_expr();
			menu_add_symbol(P_DEFAULT, sym, d);
			expect(T_EOL);
		} else if (eat(T_DEPENDS)) {
			struct expr *d;
			expect(T_ON);
			d = parse_expr();
			menu_add_dep(d);
			expect(T_EOL);
		} else if (cur_tok == T_HELP) {
			parse_help();
		} else
			break;
	}
}

static void parse_choice_stmt(void)
{
	char *name = NULL;
	struct symbol *sym;
	if (cur_tok == T_WORD)
		name = eat_string();
	sym = sym_lookup(name, SYMBOL_CHOICE);
	sym->flags |= SYMBOL_NO_WRITE;
	menu_add_entry(sym);
	menu_add_expr(P_CHOICE, NULL, NULL);
	free(name);
	expect(T_EOL);
	parse_choice_options();
	menu_add_menu();
	parse_stmt_list(true);
	if (!eat(T_ENDCHOICE))
		zconf_error("expected 'endchoice'");
	else
		expect(T_EOL);
	menu_end_menu();
}

static void parse_if_stmt(bool in_choice)
{
	struct expr *d = parse_expr();
	expect(T_EOL);
	menu_add_entry(NULL);
	menu_add_dep(d);
	menu_add_menu();
	parse_stmt_list(in_choice);
	if (!eat(T_ENDIF))
		zconf_error("expected 'endif'");
	else
		expect(T_EOL);
	menu_end_menu();
}

static void parse_menu_stmt(void)
{
	char *p = eat_string();
	expect(T_EOL);
	menu_add_entry(NULL);
	menu_add_prompt(P_MENU, p, NULL);
	for (;;) {
		if (eat(T_DEPENDS)) {
			struct expr *d;
			expect(T_ON);
			d = parse_expr();
			menu_add_dep(d);
			expect(T_EOL);
		} else
			break;
	}
	menu_add_menu();
	parse_stmt_list(false);
	if (!eat(T_ENDMENU))
		zconf_error("expected 'endmenu'");
	else
		expect(T_EOL);
	menu_end_menu();
}

static void parse_comment_stmt(void)
{
	char *t = eat_string();
	expect(T_EOL);
	menu_add_entry(NULL);
	menu_add_prompt(P_COMMENT, t, NULL);
	for (;;) {
		if (eat(T_DEPENDS)) {
			struct expr *d;
			expect(T_ON);
			d = parse_expr();
			menu_add_dep(d);
			expect(T_EOL);
		} else
			break;
	}
}

static void parse_source_stmt(void)
{
	char *name = eat_string();
	if (cur_tok != T_EOL)
		zconf_error("expected newline after source filename");
	zconf_nextfile(name);
	free(name);
	prev_token_type = T_EOL;
	advance();
}

static void parse_assignment_stmt(char *name)
{
	enum variable_flavor flavor;
	char *val;
	if (cur_tok == T_EQUAL)
		flavor = VAR_RECURSIVE;
	else if (cur_tok == T_COLON_EQUAL)
		flavor = VAR_SIMPLE;
	else
		flavor = VAR_APPEND;
	val = lex_raw_line();
	/* Must add variable BEFORE advance(), since advance() may expand
	 * tokens on the next line that reference this variable. */
	variable_add(name, val, flavor);
	free(name);
	free(val);
	prev_token_type = cur_tok;
	advance();
	if (cur_tok == T_EOL)
		advance();
}

static void parse_stmt_list(bool in_choice)
{
	for (;;) {
		switch (cur_tok) {
		case T_CONFIG:
			advance();
			parse_config_stmt();
			break;
		case T_MENUCONFIG:
			if (in_choice) {
				zconf_error("menuconfig inside choice");
				skip_to_eol();
				break;
			}
			advance();
			parse_menuconfig_stmt();
			break;
		case T_CHOICE:
			if (in_choice) {
				zconf_error("nested choice");
				skip_to_eol();
				break;
			}
			advance();
			parse_choice_stmt();
			break;
		case T_IF:
			advance();
			parse_if_stmt(in_choice);
			break;
		case T_MENU:
			if (in_choice) {
				zconf_error("menu inside choice");
				skip_to_eol();
				break;
			}
			advance();
			parse_menu_stmt();
			break;
		case T_COMMENT:
			advance();
			parse_comment_stmt();
			break;
		case T_SOURCE:
			if (in_choice) {
				zconf_error("source inside choice");
				skip_to_eol();
				break;
			}
			advance();
			parse_source_stmt();
			break;
		case T_WORD: {
			char *name;
			if (!in_choice) {
				name = eat_string();
				if (cur_tok == T_EQUAL ||
				    cur_tok == T_COLON_EQUAL ||
				    cur_tok == T_PLUS_EQUAL) {
					parse_assignment_stmt(name);
					break;
				}
				zconf_error("unknown statement \"%s\"", name);
				free(name);
				skip_to_eol();
			} else {
				zconf_error("unexpected token in choice block");
				skip_to_eol();
			}
			break;
		}
		case T_MAINMENU: {
			char *p;
			advance();
			p = eat_string();
			menu_add_prompt(P_MENU, p, NULL);
			expect(T_EOL);
			break;
		}
		case T_ENDCHOICE:
		case T_ENDIF:
		case T_ENDMENU:
		case T_EOF:
			return;
		default:
			zconf_error("unexpected token %d", cur_tok);
			skip_to_eol();
			break;
		}
	}
}

static int yyparse(void)
{
	yynerrs = 0;
	prev_token_type = T_EOL;
	advance();
	if (eat(T_MAINMENU)) {
		char *p = eat_string();
		menu_add_prompt(P_MENU, p, NULL);
		expect(T_EOL);
	}
	parse_stmt_list(false);
	if (cur_tok != T_EOF)
		zconf_error("unexpected token at end of input");
	return yynerrs;
}

void conf_parse(const char *name)
{
	zconf_initscan(name);
	_menu_init();
	yyparse();
	variable_all_del();
	if (yynerrs)
		exit(1);
	if (!modules_sym)
		modules_sym = sym_find("n");
	if (!rootmenu.prompt) {
		current_entry = &rootmenu;
		menu_add_prompt(P_MENU, "Main menu", NULL);
	}
	menu_finalize(&rootmenu);
	conf_set_changed(true);
}
