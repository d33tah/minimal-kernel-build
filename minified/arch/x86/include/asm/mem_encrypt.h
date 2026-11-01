/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * AMD Memory Encryption Support
 *
 * Copyright (C) 2016 Advanced Micro Devices, Inc.
 *
 * Author: Tom Lendacky <thomas.lendacky@amd.com>
 */

#ifndef __X86_MEM_ENCRYPT_H__
#define __X86_MEM_ENCRYPT_H__

#include <linux/init.h>

#include <asm/bootparam.h>

#define sme_me_mask 0ULL

#define __sme_pa(x) (__pa(x) | sme_me_mask)

#endif
