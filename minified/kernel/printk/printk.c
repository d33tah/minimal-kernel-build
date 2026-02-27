
#include <linux/init.h>
#include <linux/console.h>

int oops_in_progress;
struct console *console_drivers;

void console_lock(void)
{
}

void console_unlock(void)
{
}

void console_unblank(void)
{
}

void __init console_init(void)
{
}
