// SPDX-License-Identifier: GPL-2.0
/*
 * mm/debug.c
 *
 * mm/ specific debug routines.
 *
 */

#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/trace_events.h>
#include <linux/memcontrol.h>
#include <trace/events/mmflags.h>
#include <linux/migrate.h>
#include <linux/page_owner.h>
#include <linux/ctype.h>

#include "internal.h"
#include <trace/events/migrate.h>

/*
 * Define EM() and EMe() so that MIGRATE_REASON from trace/events/migrate.h can
 * be used to populate migrate_reason_names[].
 */
#undef EM
#undef EMe
#define EM(a, b)	b,
#define EMe(a, b)	b

const char *migrate_reason_names[MR_TYPES] = {
	MIGRATE_REASON
};

const struct trace_print_flags pageflag_names[] = {
	__def_pageflag_names,
	{0, NULL}
};

const struct trace_print_flags gfpflag_names[] = {
	__def_gfpflag_names,
	{0, NULL}
};

const struct trace_print_flags vmaflag_names[] = {
	__def_vmaflag_names,
	{0, NULL}
};

void dump_page(struct page *page, const char *reason)
{
	/* Stubbed for minimization */
}
EXPORT_SYMBOL(dump_page);

