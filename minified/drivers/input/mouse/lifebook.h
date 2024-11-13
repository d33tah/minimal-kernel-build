/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Fujitsu B-series Lifebook PS/2 TouchScreen driver
 *
 * Copyright (c) 2005 Vojtech Pavlik
 */

#ifndef _LIFEBOOK_H
#define _LIFEBOOK_H

int lifebook_detect(struct psmouse *psmouse, bool set_properties);
int lifebook_init(struct psmouse *psmouse);

static inline void lifebook_module_init(void)
{
}

#endif
