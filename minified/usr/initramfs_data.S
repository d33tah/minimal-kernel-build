/* SPDX-License-Identifier: GPL-2.0 */
/* The built-in initramfs (usr/initramfs_data.cpio) and its size. */

.section .init.ramfs,"a"
__irf_start:
.incbin "usr/initramfs_data.cpio"
__irf_end:
.section .init.ramfs.info,"a"
.globl __initramfs_size
__initramfs_size:
	.long __irf_end - __irf_start
