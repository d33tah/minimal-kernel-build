// SPDX-License-Identifier: GPL-2.0
// Copyright(c) 2018 Linus Torvalds. All rights reserved.
// Copyright(c) 2018 Alexei Starovoitov. All rights reserved.
// Copyright(c) 2018 Intel Corporation. All rights reserved.

#ifndef _LINUX_NOSPEC_H
#define _LINUX_NOSPEC_H

/*
 * array_index_nospec - sanitize an array index after a bounds check
 *
 * For a code sequence like:
 *
 *     if (index < size) {
 *         index = array_index_nospec(index, size);
 *         val = array[index];
 *     }
 *
 * ...if the CPU speculates past the bounds check then
 * array_index_nospec() will clamp the index within the range of [0,
 * size).
 */
#define array_index_nospec(index, size)                                \
	({                                                             \
		typeof(index) _i = (index);                            \
		typeof(size) _s = (size);                              \
		unsigned long _mask = array_index_mask_nospec(_i, _s); \
		BUILD_BUG_ON(sizeof(_i) > sizeof(long));               \
		BUILD_BUG_ON(sizeof(_s) > sizeof(long));               \
		(typeof(_i))(_i & _mask);                              \
	})

#endif /* _LINUX_NOSPEC_H */
