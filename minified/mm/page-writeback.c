
#include <linux/kernel.h>
#include <linux/export.h>
#include <linux/spinlock.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/swap.h>
#include <linux/slab.h>
#include <linux/pagemap.h>
#include <linux/writeback.h>
#include <linux/init.h>
#include <linux/backing-dev.h>
#include <linux/task_io_accounting_ops.h>
#include <linux/blkdev.h>
#include <linux/tracepoint.h>
#include <linux/rmap.h>
#include <linux/percpu.h>
#include <linux/smp.h>
#include <linux/sysctl.h>
#include <linux/cpu.h>
#include <linux/syscalls.h>
#include <linux/pagevec.h>
#include <linux/timer.h>
#include <linux/sched/rt.h>
#include <linux/sched/signal.h>
#include <linux/mm_inline.h>

#include "internal.h"

struct wb_domain global_wb_domain;
DEFINE_PER_CPU(int, dirty_throttle_leaks);
unsigned int dirty_writeback_interval = 5 * 100;

bool __folio_end_writeback(struct folio *folio) { return true; }
bool __folio_start_writeback(struct folio *folio, bool keep_write) { return true; }

void wb_writeout_inc(struct bdi_writeback *wb) {}
int bdi_set_min_ratio(struct backing_dev_info *bdi, unsigned int min_ratio) { return 0; }
int bdi_set_max_ratio(struct backing_dev_info *bdi, unsigned max_ratio) { return 0; }
void wb_update_bandwidth(struct bdi_writeback *wb) {}
void balance_dirty_pages_ratelimited(struct address_space *mapping) {}
void __init page_writeback_init(void) {}
void laptop_mode_timer_fn(struct timer_list *t) {}
int do_writepages(struct address_space *mapping, struct writeback_control *wbc) { return 0; }
int folio_write_one(struct folio *folio) { return 0; }
bool noop_dirty_folio(struct address_space *mapping, struct folio *folio) { return true; }
void folio_account_cleaned(struct folio *folio, struct bdi_writeback *wb) {}
void __folio_mark_dirty(struct folio *folio, struct address_space *mapping, int warn) {}
bool filemap_dirty_folio(struct address_space *mapping, struct folio *folio) { return true; }
void folio_account_redirty(struct folio *folio) {}
bool folio_redirty_for_writepage(struct writeback_control *wbc, struct folio *folio) { return true; }
bool folio_mark_dirty(struct folio *folio) { return true; }
int set_page_dirty_lock(struct page *page) { return 0; }
void __folio_cancel_dirty(struct folio *folio) {}
bool folio_clear_dirty_for_io(struct folio *folio) { return true; }
void folio_wait_writeback(struct folio *folio) {}
void folio_wait_stable(struct folio *folio) {}
int folio_wait_writeback_killable(struct folio *folio) { return 0; }
