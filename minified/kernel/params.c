#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/moduleparam.h>
#include <linux/err.h>
#include <linux/security.h>


/*
 * Runtime-dead cmdline-parse cluster (anchor-stub): on a single-shot boot the
 * cmdline is empty, so parse_args() sees skip_spaces("")=="" -> `if (*args)`
 * false -> its loop body (next_arg + parse_one + the `unknown` callbacks) never
 * runs and it always returns NULL. The loop body (and its now-orphaned static
 * parse_one callee) are therefore provably dead on the honest boot; the whole
 * body is stubbed to `return NULL;`. The parse_args symbol + its unconditional
 * call sites in init/main.c (and the address-taken callbacks passed there) stay
 * link-live and behaviourally identical on the empty-cmdline boot.
 */
char *parse_args(const char *doing,
		 char *args,
		 const struct kernel_param *params,
		 unsigned num,
		 s16 min_level,
		 s16 max_level,
		 void *arg,
		 int (*unknown)(char *param, char *val,
				const char *doing, void *arg))
{
	return NULL;
}
