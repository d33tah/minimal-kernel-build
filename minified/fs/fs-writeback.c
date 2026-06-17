/* Minimal includes for writeback stubs */
#include <linux/writeback.h>
#include <linux/backing-dev.h>

int dirty_writeback_centisecs = 500;
int dirty_expire_centisecs = 3000;
