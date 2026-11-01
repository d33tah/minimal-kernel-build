/* SPDX-License-Identifier: GPL-2.0-only */
/* -*- linux-c -*- ------------------------------------------------------- *
 *
 *   Copyright (C) 1991, 1992 Linus Torvalds
 *   Copyright 2007 rPath, Inc. - All Rights Reserved
 *
 * ----------------------------------------------------------------------- */

/*
 * Header file for the real-mode video probing code
 */

#ifndef BOOT_VIDEO_H
#define BOOT_VIDEO_H

#include <linux/types.h>

void vga_probe(void);

/* Basic video information */
#define ADAPTER_CGA 0 /* CGA/MDA/HGC */
#define ADAPTER_EGA 1

extern int adapter;

#endif /* BOOT_VIDEO_H */
