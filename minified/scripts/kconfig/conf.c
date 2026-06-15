
#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <getopt.h>
#include <sys/time.h>
#include <errno.h>

#include "lkc.h"

enum input_mode {
	syncconfig,
	allnoconfig,
	olddefconfig,
};
static enum input_mode input_mode = allnoconfig;
static int input_mode_opt;
static int tty_stdio;
static int sync_kconfig;

enum conf_def_mode {
	def_default,
	def_yes,
	def_mod,
	def_no,
	def_random
};

static bool conf_set_all_new_symbols(enum conf_def_mode mode)
{
	struct symbol *sym, *csym;
	int i;
	bool has_changed = false;

	for_all_symbols(i, sym) {
		if (sym_has_value(sym) || sym->flags & SYMBOL_VALID)
			continue;
		switch (sym_get_type(sym)) {
		case S_BOOLEAN:
		case S_TRISTATE:
			has_changed = true;
			switch (mode) {
			case def_no:
				sym->def[S_DEF_USER].tri = no;
				break;
			default:
				continue;
			}
			if (!(sym_is_choice(sym) && mode == def_random))
				sym->flags |= SYMBOL_DEF_USER;
			break;
		default:
			break;
		}

	}

	sym_clear_all_valid();


	for_all_symbols(i, csym) {
		if ((sym_is_choice(csym) && !sym_has_value(csym)) ||
		    sym_is_choice_value(csym))
			csym->flags |= SYMBOL_NEED_SET_CHOICE_VALUES;
	}

	for_all_symbols(i, csym) {
		if (sym_has_value(csym) || !sym_is_choice(csym))
			continue;

		sym_calc_value(csym);
		set_all_choice_values(csym);
		has_changed = true;
	}

	return has_changed;
}

static const struct option long_opts[] = {
	{"help",          no_argument,       NULL,            'h'},
	{"silent",        no_argument,       NULL,            's'},
	{"syncconfig",    no_argument,       &input_mode_opt, syncconfig},
	{"allnoconfig",   no_argument,       &input_mode_opt, allnoconfig},
	{"olddefconfig",  no_argument,       &input_mode_opt, olddefconfig},
	{NULL, 0, NULL, 0}
};

static void conf_usage(const char *progname)
{
	printf("Usage: %s [options] <kconfig-file>\n", progname);
	printf("  -h, --help, -s, --silent\n");
	printf("  --syncconfig, --allnoconfig, --olddefconfig\n");
}

int main(int ac, char **av)
{
	const char *progname = av[0];
	int opt;
	const char *name;
	int no_conf_write = 0;

	tty_stdio = isatty(0) && isatty(1);

	while ((opt = getopt_long(ac, av, "hs", long_opts, NULL)) != -1) {
		switch (opt) {
		case 'h':
			conf_usage(progname);
			exit(1);
			break;
		case 's':
			conf_set_message_callback(NULL);
			break;
		case 0:
			input_mode = input_mode_opt;
			if (input_mode == syncconfig) {
				conf_set_message_callback(NULL);
				sync_kconfig = 1;
			}
			break;
		default:
			break;
		}
	}
	if (ac == optind) {
		fprintf(stderr, "%s: Kconfig file missing\n", av[0]);
		conf_usage(progname);
		exit(1);
	}
	conf_parse(av[optind]);


	switch (input_mode) {
	case syncconfig:
	case olddefconfig:
		conf_read(NULL);
		break;
	case allnoconfig:
		name = getenv("KCONFIG_ALLCONFIG");
		if (!name)
			break;
		if ((strcmp(name, "") != 0) && (strcmp(name, "1") != 0)) {
			if (conf_read_simple(name, S_DEF_USER)) {
				fprintf(stderr,
					"*** Can't read seed configuration \"%s\"!\n",
					name);
				exit(1);
			}
			break;
		}
		name = "allno.config";
		if (conf_read_simple(name, S_DEF_USER) &&
		    conf_read_simple("all.config", S_DEF_USER)) {
			fprintf(stderr,
				"*** KCONFIG_ALLCONFIG set, but no \"%s\" or \"all.config\" file found\n",
				name);
			exit(1);
		}
		break;
	default:
		break;
	}

	if (sync_kconfig) {
		name = getenv("KCONFIG_NOSILENTUPDATE");
		if (name && *name) {
			if (conf_get_changed()) {
				fprintf(stderr,
					"\n*** The configuration requires explicit update.\n\n");
				return 1;
			}
			no_conf_write = 1;
		}
	}

	switch (input_mode) {
	case allnoconfig:
		conf_set_all_new_symbols(def_no);
		break;
	case syncconfig:
	case olddefconfig:
	default:
		break;
	}

	if (!no_conf_write && conf_write(NULL)) {
		fprintf(stderr, "\n*** Error during writing of the configuration.\n\n");
		exit(1);
	}


	if (conf_write_autoconf(sync_kconfig) && sync_kconfig) {
		fprintf(stderr,
			"\n*** Error during sync of the configuration.\n\n");
		return 1;
	}
	return 0;
}
