// SPDX-License-Identifier: GPL-2.0-only
/* -*- linux-c -*- ------------------------------------------------------- *
 *
 *   Copyright (C) 1991, 1992 Linus Torvalds
 *   Copyright 2007-2008 rPath, Inc. - All Rights Reserved
 *
 * ----------------------------------------------------------------------- */

/*
 * arch/i386/boot/video-mode.c
 *
 * Set the video mode.  This is separated out into a different
 * file in order to be shared with the ACPI wakeup code.
 */

#include "boot.h"
#include "video.h"

/*
 * Common variables
 */
int adapter; /* 0=CGA/MDA/HGC, 1=EGA, 2=VGA+ */
