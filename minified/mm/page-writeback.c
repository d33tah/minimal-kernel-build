/* Stub page writeback - no actual writeback */
#include <linux/writeback.h>
#include <linux/backing-dev.h>
#include <linux/percpu.h>

DEFINE_PER_CPU(int, dirty_throttle_leaks);

bool __folio_start_writeback(struct folio *folio, bool keep_write) { return true; }

void balance_dirty_pages_ratelimited(struct address_space *mapping) {}
void __init page_writeback_init(void) {}
void folio_account_cleaned(struct folio *folio, struct bdi_writeback *wb) {}
bool folio_mark_dirty(struct folio *folio) { return true; }
void __folio_cancel_dirty(struct folio *folio) {}
void folio_wait_writeback(struct folio *folio) {}
void folio_wait_stable(struct folio *folio) {}
