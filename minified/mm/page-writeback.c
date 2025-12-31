/* Stub page writeback - no actual writeback */
#include <linux/writeback.h>
#include <linux/backing-dev.h>
#include <linux/percpu.h>

struct wb_domain global_wb_domain;
DEFINE_PER_CPU(int, dirty_throttle_leaks);
unsigned int dirty_writeback_interval = 5 * 100;

bool __folio_end_writeback(struct folio *folio)
{
	return true;
}
bool __folio_start_writeback(struct folio *folio, bool keep_write)
{
	return true;
}

int bdi_set_min_ratio(struct backing_dev_info *bdi, unsigned int min_ratio)
{
	return 0;
}
/* bdi_set_max_ratio removed - never called */
void wb_update_bandwidth(struct bdi_writeback *wb)
{
}
/* balance_dirty_pages_ratelimited removed - never called */
void __init page_writeback_init(void)
{
}
void laptop_mode_timer_fn(struct timer_list *t)
{
}
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
