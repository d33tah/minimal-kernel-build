/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _PRINTK_BRAILLE_H
#define _PRINTK_BRAILLE_H


static inline void
braille_set_options(struct console_cmdline *c, char *brl_options)
{
}

static inline int
_braille_console_setup(char **str, char **brl_options)
{
	return 0;
}

static inline int
_braille_register_console(struct console *console, struct console_cmdline *c)
{
	return 0;
}

static inline int
_braille_unregister_console(struct console *console)
{
	return 0;
}


#endif
