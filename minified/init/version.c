
#include <generated/compile.h>
#include <uapi/linux/elf.h>
#define _ELFNOTE_PASTE(a, b) a##b
#define _ELFNOTE(size, name, unique, type, desc)                            \
	static const struct {                                               \
		struct elf##size##_note _nhdr;                              \
		unsigned char _name[sizeof(name)]                           \
			__attribute__((aligned(sizeof(Elf##size##_Word)))); \
		typeof(desc) _desc                                          \
			__attribute__((aligned(sizeof(Elf##size##_Word)))); \
	} _ELFNOTE_PASTE(_note_, unique) __used                             \
		__attribute__((section(".note." name),                      \
			       aligned(sizeof(Elf##size##_Word)),           \
			       unused)) = { {                               \
						    sizeof(name),           \
						    sizeof(desc),           \
						    type,                   \
					    },                              \
					    name,                           \
					    desc }
#define ELFNOTE(size, name, type, desc) \
	_ELFNOTE(size, name, __LINE__, type, desc)
#define ELFNOTE32(name, type, desc) ELFNOTE(32, name, type, desc)
/* end elfnote.h */

#define LINUX_ELFNOTE_BUILD_SALT 0x100
#define BUILD_SALT \
	ELFNOTE32("Linux", LINUX_ELFNOTE_BUILD_SALT, CONFIG_BUILD_SALT)
#define LINUX_ELFNOTE_LTO_INFO 0x101
#define BUILD_LTO_INFO ELFNOTE32("Linux", LINUX_ELFNOTE_LTO_INFO, 0)

#ifndef UTS_SYSNAME
#define UTS_SYSNAME "Linux"
#endif
#ifndef UTS_NODENAME
#define UTS_NODENAME CONFIG_DEFAULT_HOSTNAME
#endif
#ifndef UTS_DOMAINNAME
#define UTS_DOMAINNAME "(none)"
#endif
#include <linux/utsname.h>
#include <generated/utsrelease.h>
struct uts_namespace init_uts_ns = {
	.name = {
		.sysname	= UTS_SYSNAME,
		.nodename	= UTS_NODENAME,
		.release	= UTS_RELEASE,
		.version	= UTS_VERSION,
		.machine	= UTS_MACHINE,
		.domainname	= UTS_DOMAINNAME,
	},
};

const char linux_banner[] = "Linux version " UTS_RELEASE " (" LINUX_COMPILE_BY
			    "@" LINUX_COMPILE_HOST ") (" LINUX_COMPILER
			    ") " UTS_VERSION "\n";

BUILD_SALT;
BUILD_LTO_INFO;
