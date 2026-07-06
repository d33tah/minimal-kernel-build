#ifndef PAGE_FLAGS_LAYOUT_H
#define PAGE_FLAGS_LAYOUT_H

#include <linux/numa.h>
#include <generated/bounds.h>

#define ZONES_SHIFT 1

#define ZONES_WIDTH		ZONES_SHIFT

#ifndef BUILD_VDSO32_64
#define SECTIONS_WIDTH		0

#define NODES_WIDTH		NODES_SHIFT

#define KASAN_TAG_WIDTH 0

#define LAST_CPUPID_SHIFT 0

#define LAST_CPUPID_WIDTH LAST_CPUPID_SHIFT

#endif
#endif  
