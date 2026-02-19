 
#ifndef BOOT_BOOT_H
#define BOOT_BOOT_H

#define STACK_SIZE	1024	 

#ifndef __ASSEMBLY__

#include <linux/stdarg.h>
#include <linux/types.h>
#include <asm/setup.h>
#include <asm/asm.h>
#include "bitops.h"
#include "ctype.h"
#include <asm/cpufeatures.h>
#include <asm/processor-flags.h>
struct cpu_features {
	int level;
	int family;
	int model;
	u32 flags[NCAPINTS];
};
extern struct cpu_features cpu;
extern u32 cpu_vendor[3];
int has_eflag(unsigned long mask);
void get_cpuflags(void);
void cpuid_count(u32 id, u32 count, u32 *a, u32 *b, u32 *c, u32 *d);
#include "io.h"

#define ARRAY_SIZE(x) (sizeof(x) / sizeof(*(x)))

extern struct setup_header hdr;
extern struct boot_params boot_params;

#define cpu_relax()	asm volatile("rep; nop")

static inline void io_delay(void)
{
	const u16 DELAY_PORT = 0x80;
	outb(0, DELAY_PORT);
}

static inline u16 ds(void)
{
	u16 seg;
	asm("movw %%ds,%0" : "=rm" (seg));
	return seg;
}

static inline void set_fs(u16 seg)
{
	asm volatile("movw %0,%%fs" : : "rm" (seg));
}
static inline u16 fs(void)
{
	u16 seg;
	asm volatile("movw %%fs,%0" : "=rm" (seg));
	return seg;
}

static inline void set_gs(u16 seg)
{
	asm volatile("movw %0,%%gs" : : "rm" (seg));
}
static inline u16 gs(void)
{
	u16 seg;
	asm volatile("movw %%gs,%0" : "=rm" (seg));
	return seg;
}

typedef unsigned int addr_t;

static inline u8 rdfs8(addr_t addr)
{
	u8 *ptr = (u8 *)absolute_pointer(addr);
	u8 v;
	asm volatile("movb %%fs:%1,%0" : "=q" (v) : "m" (*ptr));
	return v;
}
static inline u16 rdfs16(addr_t addr)
{
	u16 *ptr = (u16 *)absolute_pointer(addr);
	u16 v;
	asm volatile("movw %%fs:%1,%0" : "=r" (v) : "m" (*ptr));
	return v;
}
static inline u32 rdfs32(addr_t addr)
{
	u32 *ptr = (u32 *)absolute_pointer(addr);
	u32 v;
	asm volatile("movl %%fs:%1,%0" : "=r" (v) : "m" (*ptr));
	return v;
}

static inline void wrfs32(u32 v, addr_t addr)
{
	u32 *ptr = (u32 *)absolute_pointer(addr);
	asm volatile("movl %1,%%fs:%0" : "+m" (*ptr) : "ri" (v));
}

static inline u32 rdgs32(addr_t addr)
{
	u32 *ptr = (u32 *)absolute_pointer(addr);
	u32 v;
	asm volatile("movl %%gs:%1,%0" : "=r" (v) : "m" (*ptr));
	return v;
}

extern char _end[];
extern char *HEAP;
extern char *heap_end;
#define RESET_HEAP() ((void *)( HEAP = _end ))

int enable_a20(void);

struct biosregs {
	union {
		struct {
			u32 edi;
			u32 esi;
			u32 ebp;
			u32 _esp;
			u32 ebx;
			u32 edx;
			u32 ecx;
			u32 eax;
			u32 _fsgs;
			u32 _dses;
			u32 eflags;
		};
		struct {
			u16 di, hdi;
			u16 si, hsi;
			u16 bp, hbp;
			u16 _sp, _hsp;
			u16 bx, hbx;
			u16 dx, hdx;
			u16 cx, hcx;
			u16 ax, hax;
			u16 gs, fs;
			u16 es, ds;
			u16 flags, hflags;
		};
		struct {
			u8 dil, dih, edi2, edi3;
			u8 sil, sih, esi2, esi3;
			u8 bpl, bph, ebp2, ebp3;
			u8 _spl, _sph, _esp2, _esp3;
			u8 bl, bh, ebx2, ebx3;
			u8 dl, dh, edx2, edx3;
			u8 cl, ch, ecx2, ecx3;
			u8 al, ah, eax2, eax3;
		};
	};
};
void intcall(u8 int_no, const struct biosregs *ireg, struct biosregs *oreg);

int __cmdline_find_option_bool(unsigned long cmdline_ptr, const char *option);
static inline int cmdline_find_option_bool(const char *option)
{
	unsigned long cmd_line_ptr = boot_params.hdr.cmd_line_ptr;

	if (cmd_line_ptr >= 0x100000)
		return -1;

	return __cmdline_find_option_bool(cmd_line_ptr, option);
}

int check_cpu(int *cpu_level_ptr, int *req_level_ptr, u32 **err_flags_ptr);
int check_knl_erratum(void);
int validate_cpu(void);

void __attribute__((noreturn)) die(void);

void detect_memory(void);

void __attribute__((noreturn)) go_to_protected_mode(void);

void __attribute__((noreturn))
	protected_mode_jump(u32 entrypoint, u32 bootparams);

int sprintf(char *buf, const char *fmt, ...);
int vsprintf(char *buf, const char *fmt, va_list args);
int printf(const char *fmt, ...);

void initregs(struct biosregs *regs);


void puts(const char *);
void putchar(int);

void set_video(void);

#endif

#endif  
