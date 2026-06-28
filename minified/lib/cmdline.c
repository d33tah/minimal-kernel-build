
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/ctype.h>

/*
 * Runtime-dead: the only caller is the parse_args() loop body, which never runs
 * on a single-shot boot (empty cmdline -> `if (*args)` false). Stubbed; symbol
 * kept link-live for the parse_args reference + header extern.
 */
char *next_arg(char *args, char **param, char **val)
{
	*param = args;
	*val = NULL;
	return args;
}
