
#include <linux/srcu.h>

static LIST_HEAD(srcu_boot_list);
static bool srcu_init_done;

void __init srcu_init(void)
{
	struct srcu_struct *ssp;

	srcu_init_done = true;
	while (!list_empty(&srcu_boot_list)) {
		ssp = list_first_entry(&srcu_boot_list, struct srcu_struct,
				       srcu_work.entry);
		list_del_init(&ssp->srcu_work.entry);
		schedule_work(&ssp->srcu_work);
	}
}
