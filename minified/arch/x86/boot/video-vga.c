// SPDX-License-Identifier: GPL-2.0-only
/* -*- linux-c -*- ------------------------------------------------------- *
 *
 *   Copyright (C) 1991, 1992 Linus Torvalds
 *   Copyright 2007 rPath, Inc. - All Rights Reserved
 *   Copyright 2009 Intel Corporation; author H. Peter Anvin
 *
 * ----------------------------------------------------------------------- */

/*
 * Common all-VGA modes
 */

#include "boot.h"
#include "video.h"

void vga_probe(void)
{
	struct biosregs ireg, oreg;

	initregs(&ireg);

	ireg.ax = 0x1200;
	ireg.bl = 0x10; /* Check EGA/VGA */
	intcall(0x10, &ireg, &oreg);

	/* If we have MDA/CGA/HGC then BL will be unchanged at 0x10 */
	if (oreg.bl != 0x10) {
		/* EGA/VGA */
		adapter = ADAPTER_EGA;
	} else {
		adapter = ADAPTER_CGA;
	}
}
