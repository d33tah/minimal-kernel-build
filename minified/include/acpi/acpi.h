/* SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0 */
/******************************************************************************
 *
 * Name: acpi.h - Master public include file used to interface to ACPICA
 *
 * Copyright (C) 2000 - 2022, Intel Corp.
 *
 *****************************************************************************/

#ifndef __ACPI_H__
#define __ACPI_H__

/*
 * Public include files for use by code that will interface to ACPICA.
 *
 * Information includes the ACPICA data types, names, exceptions, and
 * external interface prototypes. Also included are the definitions for
 * all ACPI tables (FADT, MADT, etc.)
 *
 * Note: The order of these include files is important.
 */
#include <acpi/platform/acenv.h>	/* Environment-specific items */
		/* Common ACPI names and strings */
#include <acpi/actypes.h>		/* ACPICA data types and structures */
#include <acpi/acexcep.h>		/* ACPICA exceptions */
#include <acpi/actbl.h>		/* ACPI table definitions */
		/* Resource Descriptor structs */
	/* Extra environment-specific items */
		/* Error output and Debug macros */
		/* OSL interfaces (ACPICA-to-OS) */
		/* ACPI core subsystem external interfaces */

#endif				/* __ACPI_H__ */
