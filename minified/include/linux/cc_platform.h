/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Confidential Computing Platform Capability checks
 *
 * Copyright (C) 2021 Advanced Micro Devices, Inc.
 *
 * Author: Tom Lendacky <thomas.lendacky@amd.com>
 */

#ifndef _LINUX_CC_PLATFORM_H
#define _LINUX_CC_PLATFORM_H

#include <linux/types.h>
#include <linux/stddef.h>

/**
 * enum cc_attr - Confidential computing attributes
 *
 * These attributes represent confidential computing features that are
 * currently active.
 */
enum cc_attr {
	/**
	 * @CC_ATTR_MEM_ENCRYPT: Memory encryption is active
	 *
	 * The platform/OS is running with active memory encryption. This
	 * includes running either as a bare-metal system or a hypervisor
	 * and actively using memory encryption or as a guest/virtual machine
	 * and actively using memory encryption.
	 *
	 * Examples include SME, SEV and SEV-ES.
	 */
	CC_ATTR_MEM_ENCRYPT,

	/**
	 * @CC_ATTR_HOST_MEM_ENCRYPT: Host memory encryption is active
	 *
	 * The platform/OS is running as a bare-metal system or a hypervisor
	 * and actively using memory encryption.
	 *
	 * Examples include SME.
	 */
	CC_ATTR_HOST_MEM_ENCRYPT,

	/**
	 * @CC_ATTR_GUEST_MEM_ENCRYPT: Guest memory encryption is active
	 *
	 * The platform/OS is running as a guest/virtual machine and actively
	 * using memory encryption.
	 *
	 * Examples include SEV and SEV-ES.
	 */
	CC_ATTR_GUEST_MEM_ENCRYPT,

	/**
	 * @CC_ATTR_GUEST_STATE_ENCRYPT: Guest state encryption is active
	 *
	 * The platform/OS is running as a guest/virtual machine and actively
	 * using memory encryption and register state encryption.
	 *
	 * Examples include SEV-ES.
	 */
	CC_ATTR_GUEST_STATE_ENCRYPT,

	/**
	 * @CC_ATTR_GUEST_UNROLL_STRING_IO: String I/O is implemented with
	 *                                  IN/OUT instructions
	 *
	 * The platform/OS is running as a guest/virtual machine and uses
	 * IN/OUT instructions in place of string I/O.
	 *
	 * Examples include TDX guest & SEV.
	 */
	CC_ATTR_GUEST_UNROLL_STRING_IO,

	/**
	 * @CC_ATTR_SEV_SNP: Guest SNP is active.
	 *
	 * The platform/OS is running as a guest/virtual machine and actively
	 * using AMD SEV-SNP features.
	 */
	CC_ATTR_GUEST_SEV_SNP,

	/**
	 * @CC_ATTR_HOTPLUG_DISABLED: Hotplug is not supported or disabled.
	 *
	 * The platform/OS is running as a guest/virtual machine does not
	 * support CPU hotplug feature.
	 *
	 * Examples include TDX Guest.
	 */
	CC_ATTR_HOTPLUG_DISABLED,
};


static inline bool cc_platform_has(enum cc_attr attr) { return false; }


#endif	/* _LINUX_CC_PLATFORM_H */
