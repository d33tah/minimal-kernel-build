/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Focaltech TouchPad PS/2 mouse driver
 *
 * Copyright (c) 2014 Red Hat Inc.
 * Copyright (c) 2014 Mathias Gottschlag <mgottschlag@gmail.com>
 *
 * Red Hat authors:
 *
 * Hans de Goede <hdegoede@redhat.com>
 */

#ifndef _FOCALTECH_H
#define _FOCALTECH_H

int focaltech_detect(struct psmouse *psmouse, bool set_properties);

int focaltech_init(struct psmouse *psmouse);

#endif
