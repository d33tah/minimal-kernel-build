
#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <errno.h>

#include "lkc.h"

enum input_mode {
	allnoconfig,
	allyesconfig,
	allmodconfig,
	alldefconfig,
	randconfig,
	defconfig,
	savedefconfig,
	syncconfig,
	oldconfig,
	olddefconfig,
};
static enum input_mode input_mode = allnoconfig;
static int input_mode_opt;
static int sync_kconfig;

enum conf_def_mode { def_default, def_yes, def_mod, def_no, def_random };

static bool conf_set_all_new_symbols(enum conf_def_mode mode)
{
	struct symbol *sym, *csym;
	int i;
	bool has_changed = false;

	for_all_symbols(i, sym)
	{
		if (sym_has_value(sym) || sym->flags & SYMBOL_VALID)
			continue;
		switch (sym_get_type(sym)) {
		case S_BOOLEAN:
		case S_TRISTATE:
			has_changed = true;
			switch (mode) {
			case def_yes:
				sym->def[S_DEF_USER].tri = yes;
				break;
			case def_mod:
				sym->def[S_DEF_USER].tri = mod;
				break;
			case def_no:
				sym->def[S_DEF_USER].tri = no;
				break;
			default:
				continue;
			}
			if (!sym_is_choice(sym))
				sym->flags |= SYMBOL_DEF_USER;
			break;
		default:
			break;
		}
	}

	sym_clear_all_valid();

	for_all_symbols(i, csym)
	{
		if ((sym_is_choice(csym) && !sym_has_value(csym)) ||
		    sym_is_choice_value(csym))
			csym->flags |= SYMBOL_NEED_SET_CHOICE_VALUES;
	}

	for_all_symbols(i, csym)
	{
		if (sym_has_value(csym) || !sym_is_choice(csym))
			continue;

		sym_calc_value(csym);
		set_all_choice_values(csym);
		has_changed = true;
	}

	return has_changed;
}

static const struct option long_opts[] = {
	{ "help", no_argument, NULL, 'h' },
	{ "silent", no_argument, NULL, 's' },
	{ "syncconfig", no_argument, &input_mode_opt, syncconfig },
	{ "defconfig", required_argument, &input_mode_opt, defconfig },
	{ "savedefconfig", required_argument, &input_mode_opt, savedefconfig },
	{ "allnoconfig", no_argument, &input_mode_opt, allnoconfig },
	{ "allyesconfig", no_argument, &input_mode_opt, allyesconfig },
	{ "allmodconfig", no_argument, &input_mode_opt, allmodconfig },
	{ "alldefconfig", no_argument, &input_mode_opt, alldefconfig },
	{ "oldconfig", no_argument, &input_mode_opt, oldconfig },
	{ "olddefconfig", no_argument, &input_mode_opt, olddefconfig },
	{ NULL, 0, NULL, 0 }
};

static void conf_usage(const char *progname)
{
	printf("Usage: %s [options] <kconfig-file>\n", progname);
	printf("  --allnoconfig, --allyesconfig, --allmodconfig, --alldefconfig\n");
	printf("  --defconfig <file>, --savedefconfig <file>\n");
	printf("  --syncconfig, --oldconfig, --olddefconfig\n");
}

int main(int ac, char **av)
{
	const char *progname = av[0];
	int opt;
	const char *name, *defconfig_file = NULL;
	int no_conf_write = 0;

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
			switch (input_mode) {
			case syncconfig:
				conf_set_message_callback(NULL);
				sync_kconfig = 1;
				break;
			case defconfig:
			case savedefconfig:
				defconfig_file = optarg;
				break;
			default:
				break;
			}
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
	case defconfig:
		if (conf_read(defconfig_file)) {
			fprintf(stderr,
				"***\n"
				"*** Can't find default configuration \"%s\"!\n"
				"***\n",
				defconfig_file);
			exit(1);
		}
		break;
	case savedefconfig:
	case syncconfig:
	case oldconfig:
	case olddefconfig:
		conf_read(NULL);
		break;
	case allnoconfig:
	case allyesconfig:
	case allmodconfig:
	case alldefconfig:
	case randconfig:
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
		switch (input_mode) {
		case allnoconfig:
			name = "allno.config";
			break;
		case allyesconfig:
			name = "allyes.config";
			break;
		case allmodconfig:
			name = "allmod.config";
			break;
		case alldefconfig:
			name = "alldef.config";
			break;
		default:
			name = "allrandom.config";
			break;
		}
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
	case allyesconfig:
		conf_set_all_new_symbols(def_yes);
		break;
	case allmodconfig:
		conf_set_all_new_symbols(def_mod);
		break;
	case alldefconfig:
	case defconfig:
		conf_set_all_new_symbols(def_default);
		break;
	case oldconfig:
	case syncconfig:
	case olddefconfig:
	default:
		break;
	}

	if (input_mode == savedefconfig) {
		if (conf_write_defconfig(defconfig_file)) {
			fprintf(stderr,
				"n*** Error while saving defconfig to: %s\n\n",
				defconfig_file);
			return 1;
		}
	} else {
		if (!no_conf_write && conf_write(NULL)) {
			fprintf(stderr,
				"\n*** Error during writing of the configuration.\n\n");
			exit(1);
		}

		if (conf_write_autoconf(sync_kconfig) && sync_kconfig) {
			fprintf(stderr,
				"\n*** Error during sync of the configuration.\n\n");
			return 1;
		}
	}
	return 0;
}
