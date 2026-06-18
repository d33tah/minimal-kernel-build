/* Stub page writeback - no actual writeback */
#include <linux/writeback.h>
#include <linux/backing-dev.h>
#include <linux/percpu.h>

bool folio_mark_dirty(struct folio *folio) { return true; }
