#include <linux/console.h>
#include <linux/vt_kern.h>
#include <linux/module.h>

#define DUMMY_COLUMNS CONFIG_DUMMY_CONSOLE_COLUMNS
#define DUMMY_ROWS CONFIG_DUMMY_CONSOLE_ROWS

static void dummycon_putcs(struct vc_data *vc, const unsigned short *s,
			   int count, int ypos, int xpos)
{
}

static const char *dummycon_startup(void)
{
	return "dummy device";
}

static void dummycon_init(struct vc_data *vc, int init)
{
	vc->vc_can_do_color = 1;
	if (init) {
		vc->vc_cols = DUMMY_COLUMNS;
		vc->vc_rows = DUMMY_ROWS;
	} else
		vc_resize(vc, DUMMY_COLUMNS, DUMMY_ROWS);
}

static bool dummycon_scroll(struct vc_data *vc, unsigned int top,
			    unsigned int bottom, enum con_scroll dir,
			    unsigned int lines)
{
	return false;
}

static int dummycon_switch(struct vc_data *vc)
{
	return 0;
}

const struct consw dummy_con = {
	.owner = THIS_MODULE,
	.con_startup = dummycon_startup,
	.con_init = dummycon_init,
	.con_putcs = dummycon_putcs,
	.con_scroll = dummycon_scroll,
	.con_switch = dummycon_switch,
};
