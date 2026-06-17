/* Stub page writeback - no actual writeback */
#include <linux/writeback.h>
#include <linux/backing-dev.h>
#include <linux/percpu.h>

bool __folio_start_writeback(struct folio *folio, bool keep_write) { return true; }

bool folio_mark_dirty(struct folio *folio) { return true; }
