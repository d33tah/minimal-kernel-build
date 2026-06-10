/* Stub page writeback - no actual writeback */
#include <linux/writeback.h>
#include <linux/backing-dev.h>
#include <linux/percpu.h>

DEFINE_PER_CPU(int, dirty_throttle_leaks);

bool __folio_end_writeback(struct folio *folio) { return true; }
bool __folio_start_writeback(struct folio *folio, bool keep_write) { return true; }

void balance_dirty_pages_ratelimited(struct address_space *mapping) {}
void __init page_writeback_init(void) {}
int do_writepages(struct address_space *mapping, struct writeback_control *wbc) { return 0; }
bool noop_dirty_folio(struct address_space *mapping, struct folio *folio) { return true; }
void folio_account_cleaned(struct folio *folio, struct bdi_writeback *wb) {}
bool folio_mark_dirty(struct folio *folio) { return true; }
void __folio_cancel_dirty(struct folio *folio) {}
void folio_wait_writeback(struct folio *folio) {}
void folio_wait_stable(struct folio *folio) {}
