 
#ifndef _ASM_X86_SPECIAL_INSNS_H
#define _ASM_X86_SPECIAL_INSNS_H



#include <asm/nops.h>
#include <asm/processor-flags.h>
#include <linux/irqflags.h>
#include <linux/jump_label.h>

 

#define __FORCE_ORDER "m"(*(unsigned int *)0x1000UL)

void native_write_cr0(unsigned long val);

static inline unsigned long native_read_cr0(void) {
	unsigned long val;
	asm volatile("mov %%cr0,%0\n\t" : "=r" (val) : __FORCE_ORDER);
	return val; }

static __always_inline unsigned long native_read_cr2(void) {
	unsigned long val;
	asm volatile("mov %%cr2,%0\n\t" : "=r" (val) : __FORCE_ORDER);
	return val; }

static __always_inline void native_write_cr2(unsigned long val) {
	asm volatile("mov %0,%%cr2": : "r" (val) : "memory"); }

static inline unsigned long __native_read_cr3(void) {
	unsigned long val;
	asm volatile("mov %%cr3,%0\n\t" : "=r" (val) : __FORCE_ORDER);
	return val; }

static inline void native_write_cr3(unsigned long val) {
	asm volatile("mov %0,%%cr3": : "r" (val) : "memory"); }

static inline unsigned long native_read_cr4(void) {
	unsigned long val;
	 
	asm volatile("1: mov %%cr4, %0\n" "2:\n" _ASM_EXTABLE(1b, 2b) : "=r" (val) : "0" (0), __FORCE_ORDER);
	return val; }

void native_write_cr4(unsigned long val);

/* native_wbinvd / wbinvd removed - unused */

/* native_load_gs_index, asm_load_gs_index removed - unused */

static inline unsigned long __read_cr4(void) {
	return native_read_cr4(); }


static inline unsigned long read_cr0(void) {
	return native_read_cr0(); }

static inline void write_cr0(unsigned long x) {
	native_write_cr0(x); }

static __always_inline unsigned long read_cr2(void) {
	return native_read_cr2(); }

static __always_inline void write_cr2(unsigned long x) {
	native_write_cr2(x); }

 
static inline unsigned long __read_cr3(void) {
	return __native_read_cr3(); }

static inline void write_cr3(unsigned long x) {
	native_write_cr3(x); }

static inline void __write_cr4(unsigned long x) {
	native_write_cr4(x); }

/* load_gs_index removed - unused */

/* nop() removed - never invoked (all "rep; nop" uses are inline-asm string literals) */

/* serialize removed - unused (only sync_core.h called it, now removed) */

/* movdir64b removed - unused */


#endif
