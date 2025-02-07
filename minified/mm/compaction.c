// SPDX-License-Identifier: GPL-2.0
/*
 * linux/mm/compaction.c
 *
 * Memory compaction for the reduction of external fragmentation. Note that
 * this heavily depends upon page migration to do all the real heavy
 * lifting
 *
 * Copyright IBM Corp. 2007-2010 Mel Gorman <mel@csn.ul.ie>
 */

#define count_compact_event(item) do { } while (0)
#define count_compact_events(item, delta) do { } while (0)

