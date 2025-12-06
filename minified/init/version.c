
#include <generated/compile.h>
#include <linux/build-salt.h>
#include <linux/elfnote-lto.h>
#include <linux/export.h>

/* --- 2025-12-06 20:37 --- uts.h inlined (16 LOC) */
#ifndef UTS_SYSNAME
#define UTS_SYSNAME "Linux"
#endif
#ifndef UTS_NODENAME
#define UTS_NODENAME CONFIG_DEFAULT_HOSTNAME
#endif
#ifndef UTS_DOMAINNAME
#define UTS_DOMAINNAME "(none)"
#endif
/* --- end uts.h inlined --- */
#include <linux/utsname.h>
#include <generated/utsrelease.h>
#include <linux/version.h>
#include <linux/proc_ns.h>

struct uts_namespace init_uts_ns = {
	.ns.count = REFCOUNT_INIT(2),
	.name = {
		.sysname	= UTS_SYSNAME,
		.nodename	= UTS_NODENAME,
		.release	= UTS_RELEASE,
		.version	= UTS_VERSION,
		.machine	= UTS_MACHINE,
		.domainname	= UTS_DOMAINNAME,
	},
	.user_ns = &init_user_ns,
	.ns.inum = PROC_UTS_INIT_INO,
};

const char linux_banner[] =
	"Linux version " UTS_RELEASE " (" LINUX_COMPILE_BY "@"
	LINUX_COMPILE_HOST ") (" LINUX_COMPILER ") " UTS_VERSION "\n";

const char linux_proc_banner[] =
	"%s version %s"
	" (" LINUX_COMPILE_BY "@" LINUX_COMPILE_HOST ")"
	" (" LINUX_COMPILER ") %s\n";

BUILD_SALT;
BUILD_LTO_INFO;
