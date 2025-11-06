// SPDX-License-Identifier: GPL-2.0
/*
 * Minimal stub - TSX not needed
 */
#include <linux/cpu.h>
#include <asm/processor.h>

#include "cpu.h"

void __init tsx_init(void)
{
}

void tsx_ap_init(void)
{
}
