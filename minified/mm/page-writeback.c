/* Stub page writeback - no actual writeback */
#include <linux/writeback.h>
#include <linux/backing-dev.h>
#include <linux/percpu.h>

DEFINE_PER_CPU(int, dirty_throttle_leaks);
/* global_wb_domain removed - never used */

bool __folio_end_writeback(struct folio *folio)
{
	return true;
}
/* __folio_start_writeback removed - never called */

int bdi_set_min_ratio(struct backing_dev_info *bdi, unsigned int min_ratio)
{
	return 0;
}
/* bdi_set_max_ratio removed - never called */
void wb_update_bandwidth(struct bdi_writeback *wb)
{
}
/* balance_dirty_pages_ratelimited, page_writeback_init, laptop_mode_timer_fn removed - empty stubs */
int do_writepages(struct address_space *mapping, struct writeback_control *wbc)
{
	return 0;
}
/* noop_dirty_folio, filemap_dirty_folio removed - unused */
void folio_account_cleaned(struct folio *folio, struct bdi_writeback *wb)
{
}
bool folio_mark_dirty(struct folio *folio)
{
	return true;
}
void __folio_cancel_dirty(struct folio *folio)
{
}
void folio_wait_writeback(struct folio *folio)
{
}
void folio_wait_stable(struct folio *folio)
{
}
