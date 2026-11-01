/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LINUX_SMP_H
#define __LINUX_SMP_H

/*
 *	These macros fold the SMP functionality into a single CPU system
 */
#define raw_smp_processor_id() 0

#define __smp_processor_id(x) raw_smp_processor_id(x)

#define smp_processor_id() __smp_processor_id()

#endif
