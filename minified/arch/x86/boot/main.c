// SPDX-License-Identifier: GPL-2.0-only
/* -*- linux-c -*- ------------------------------------------------------- *
 *
 *   Copyright (C) 1991, 1992 Linus Torvalds
 *   Copyright 2007 rPath, Inc. - All Rights Reserved
 *   Copyright 2009 Intel Corporation; author H. Peter Anvin
 *
 * ----------------------------------------------------------------------- */

/*
 * Main module for the real-mode kernel code
 */
#include <linux/build_bug.h>

#include "boot.h"
#include "string.h"

struct boot_params boot_params __attribute__((aligned(16)));

struct port_io_ops pio_ops;

/*
 * Copy the header into the boot parameter block.  Since this
 * screws up the old-style command line protocol, adjust by
 * filling in the new-style command line pointer instead.
 */

static void copy_boot_params(void)
{
	BUILD_BUG_ON(sizeof(boot_params) != 4096);
	memcpy(&boot_params.hdr, &hdr, sizeof(hdr));
}

void main(void)
{
	init_default_io_ops();

	/* First, copy the boot header into the "zeropage" */
	copy_boot_params();

	/* Detect memory layout */
	detect_memory();

	/* Set the video mode */
	set_video();

	/* Do the last things and invoke protected mode */
	go_to_protected_mode();
}
