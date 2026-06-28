/* Minimal includes for dummycon */
#include <linux/console.h>
#include <linux/vt_kern.h>


#define DUMMY_COLUMNS	CONFIG_DUMMY_CONSOLE_COLUMNS
#define DUMMY_ROWS	CONFIG_DUMMY_CONSOLE_ROWS

static void dummycon_putc(struct vc_data *vc, int c, int ypos, int xpos) { }
static void dummycon_putcs(struct vc_data *vc, const unsigned short *s,
			   int count, int ypos, int xpos) { }

/*
 * The whole dummy_con consw table is runtime-dead (HIT=False): the dummy
 * console is only bound as a fallback when no real console is available; the
 * VGA console (vga_con) is live on this boot, so dummy_con's ops never fire.
 * Stub the two non-trivial bodies; symbols kept for the dummy_con fn-ptr table.
 */
static const char *dummycon_startup(void)
{
    return "dummy device";
}

static void dummycon_init(struct vc_data *vc, int init) { }

static void dummycon_deinit(struct vc_data *vc) { }
static void dummycon_cursor(struct vc_data *vc, int mode) { }

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
	.con_startup =	dummycon_startup,
	.con_init =		dummycon_init,
	.con_deinit =	dummycon_deinit,
	.con_putc =		dummycon_putc,
	.con_putcs =	dummycon_putcs,
	.con_cursor =	dummycon_cursor,
	.con_scroll =	dummycon_scroll,
	.con_switch =	dummycon_switch,
};
