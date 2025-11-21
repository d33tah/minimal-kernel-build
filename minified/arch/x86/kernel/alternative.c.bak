 
#define pr_fmt(fmt) "SMP alternatives: " fmt

#include <linux/module.h>
#include <linux/sched.h>
#include <linux/perf_event.h>
#include <linux/mutex.h>
#include <linux/list.h>
#include <linux/stringify.h>
#include <linux/highmem.h>
#include <linux/mm.h>
#include <linux/vmalloc.h>
#include <linux/memory.h>
#include <linux/stop_machine.h>
#include <linux/slab.h>
#include <linux/kdebug.h>
#include <linux/kprobes.h>
#include <linux/mmu_context.h>
#include <linux/bsearch.h>
#include <linux/sync_core.h>
#include <asm/text-patching.h>
#include <asm/alternative.h>
#include <asm/sections.h>
#include <asm/mce.h>
#include <asm/nmi.h>
#include <asm/cacheflush.h>
#include <asm/tlbflush.h>
#include <asm/insn.h>
#include <asm/io.h>
#include <asm/fixmap.h>
#include <asm/paravirt.h>
#include <asm/asm-prototypes.h>

int __read_mostly alternatives_patched;


#define MAX_PATCH_LEN (255-1)

static int __initdata_or_module debug_alternative;

static int __init debug_alt(char *str)
{
	debug_alternative = 1;
	return 1;
}
__setup("debug-alternative", debug_alt);

static int noreplace_smp;

static int __init setup_noreplace_smp(char *str)
{
	noreplace_smp = 1;
	return 1;
}
__setup("noreplace-smp", setup_noreplace_smp);

#define DPRINTK(fmt, args...)						\
do {									\
	if (debug_alternative)						\
		printk(KERN_DEBUG pr_fmt(fmt) "\n", ##args);		\
} while (0)

#define DUMP_BYTES(buf, len, fmt, args...)				\
do {									\
	if (unlikely(debug_alternative)) {				\
		int j;							\
									\
		if (!(len))						\
			break;						\
									\
		printk(KERN_DEBUG pr_fmt(fmt), ##args);			\
		for (j = 0; j < (len) - 1; j++)				\
			printk(KERN_CONT "%02hhx ", buf[j]);		\
		printk(KERN_CONT "%02hhx\n", buf[j]);			\
	}								\
} while (0)

static const unsigned char x86nops[] =
{
	BYTES_NOP1,
	BYTES_NOP2,
	BYTES_NOP3,
	BYTES_NOP4,
	BYTES_NOP5,
	BYTES_NOP6,
	BYTES_NOP7,
	BYTES_NOP8,
};

const unsigned char * const x86_nops[ASM_NOP_MAX+1] =
{
	NULL,
	x86nops,
	x86nops + 1,
	x86nops + 1 + 2,
	x86nops + 1 + 2 + 3,
	x86nops + 1 + 2 + 3 + 4,
	x86nops + 1 + 2 + 3 + 4 + 5,
	x86nops + 1 + 2 + 3 + 4 + 5 + 6,
	x86nops + 1 + 2 + 3 + 4 + 5 + 6 + 7,
};

 
static void __init_or_module add_nops(void *insns, unsigned int len)
{
	while (len > 0) {
		unsigned int noplen = len;
		if (noplen > ASM_NOP_MAX)
			noplen = ASM_NOP_MAX;
		memcpy(insns, x86_nops[noplen], noplen);
		insns += noplen;
		len -= noplen;
	}
}

extern s32 __retpoline_sites[], __retpoline_sites_end[];
extern s32 __return_sites[], __return_sites_end[];
extern s32 __ibt_endbr_seal[], __ibt_endbr_seal_end[];
extern struct alt_instr __alt_instructions[], __alt_instructions_end[];
extern s32 __smp_locks[], __smp_locks_end[];
void text_poke_early(void *addr, const void *opcode, size_t len);

 
static inline bool is_jmp(const u8 opcode)
{
	return opcode == 0xeb || opcode == 0xe9;
}

static void __init_or_module
recompute_jump(struct alt_instr *a, u8 *orig_insn, u8 *repl_insn, u8 *insn_buff)
{
	u8 *next_rip, *tgt_rip;
	s32 n_dspl, o_dspl;
	int repl_len;

	if (a->replacementlen != 5)
		return;

	o_dspl = *(s32 *)(insn_buff + 1);

	 
	next_rip = repl_insn + a->replacementlen;
	 
	tgt_rip  = next_rip + o_dspl;
	n_dspl = tgt_rip - orig_insn;

	DPRINTK("target RIP: %px, new_displ: 0x%x", tgt_rip, n_dspl);

	if (tgt_rip - orig_insn >= 0) {
		if (n_dspl - 2 <= 127)
			goto two_byte_jmp;
		else
			goto five_byte_jmp;
	 
	} else {
		if (((n_dspl - 2) & 0xff) == (n_dspl - 2))
			goto two_byte_jmp;
		else
			goto five_byte_jmp;
	}

two_byte_jmp:
	n_dspl -= 2;

	insn_buff[0] = 0xeb;
	insn_buff[1] = (s8)n_dspl;
	add_nops(insn_buff + 2, 3);

	repl_len = 2;
	goto done;

five_byte_jmp:
	n_dspl -= 5;

	insn_buff[0] = 0xe9;
	*(s32 *)&insn_buff[1] = n_dspl;

	repl_len = 5;

done:

	DPRINTK("final displ: 0x%08x, JMP 0x%lx",
		n_dspl, (unsigned long)orig_insn + n_dspl + repl_len);
}

 
static __always_inline int optimize_nops_range(u8 *instr, u8 instrlen, int off)
{
	unsigned long flags;
	int i = off, nnops;

	while (i < instrlen) {
		if (instr[i] != 0x90)
			break;

		i++;
	}

	nnops = i - off;

	if (nnops <= 1)
		return nnops;

	local_irq_save(flags);
	add_nops(instr + off, nnops);
	local_irq_restore(flags);

	DUMP_BYTES(instr, instrlen, "%px: [%d:%d) optimized NOPs: ", instr, off, i);

	return nnops;
}

 
static void __init_or_module noinline optimize_nops(u8 *instr, size_t len)
{
	struct insn insn;
	int i = 0;

	 
	for (;;) {
		if (insn_decode_kernel(&insn, &instr[i]))
			return;

		 
		if (insn.length == 1 && insn.opcode.bytes[0] == 0x90)
			i += optimize_nops_range(instr, len, i);
		else
			i += insn.length;

		if (i >= len)
			return;
	}
}

 
void __init_or_module noinline apply_alternatives(struct alt_instr *start,
						  struct alt_instr *end)
{
	struct alt_instr *a;
	u8 *instr, *replacement;
	u8 insn_buff[MAX_PATCH_LEN];

	DPRINTK("alt table %px, -> %px", start, end);
	 
	for (a = start; a < end; a++) {
		int insn_buff_sz = 0;
		 
		u16 feature = a->cpuid & ~ALTINSTR_FLAG_INV;

		instr = (u8 *)&a->instr_offset + a->instr_offset;
		replacement = (u8 *)&a->repl_offset + a->repl_offset;
		BUG_ON(a->instrlen > sizeof(insn_buff));
		BUG_ON(feature >= (NCAPINTS + NBUGINTS) * 32);

		 
		if (!boot_cpu_has(feature) == !(a->cpuid & ALTINSTR_FLAG_INV))
			goto next;

		DPRINTK("feat: %s%d*32+%d, old: (%pS (%px) len: %d), repl: (%px, len: %d)",
			(a->cpuid & ALTINSTR_FLAG_INV) ? "!" : "",
			feature >> 5,
			feature & 0x1f,
			instr, instr, a->instrlen,
			replacement, a->replacementlen);

		DUMP_BYTES(instr, a->instrlen, "%px:   old_insn: ", instr);
		DUMP_BYTES(replacement, a->replacementlen, "%px:   rpl_insn: ", replacement);

		memcpy(insn_buff, replacement, a->replacementlen);
		insn_buff_sz = a->replacementlen;

		 
		if (a->replacementlen == 5 && *insn_buff == 0xe8) {
			*(s32 *)(insn_buff + 1) += replacement - instr;
			DPRINTK("Fix CALL offset: 0x%x, CALL 0x%lx",
				*(s32 *)(insn_buff + 1),
				(unsigned long)instr + *(s32 *)(insn_buff + 1) + 5);
		}

		if (a->replacementlen && is_jmp(replacement[0]))
			recompute_jump(a, instr, replacement, insn_buff);

		for (; insn_buff_sz < a->instrlen; insn_buff_sz++)
			insn_buff[insn_buff_sz] = 0x90;

		DUMP_BYTES(insn_buff, insn_buff_sz, "%px: final_insn: ", instr);

		text_poke_early(instr, insn_buff, insn_buff_sz);

next:
		optimize_nops(instr, a->instrlen);
	}
}


void __init_or_module noinline apply_retpolines(s32 *start, s32 *end) { }
void __init_or_module noinline apply_returns(s32 *start, s32 *end) { }



void __init_or_module noinline apply_ibt_endbr(s32 *start, s32 *end) { }




 

 

extern void int3_magic(unsigned int *ptr);  

asm (
"	.pushsection	.init.text, \"ax\", @progbits\n"
"	.type		int3_magic, @function\n"
"int3_magic:\n"
	ANNOTATE_NOENDBR
"	movl	$1, (%" _ASM_ARG1 ")\n"
	ASM_RET
"	.size		int3_magic, .-int3_magic\n"
"	.popsection\n"
);

extern void int3_selftest_ip(void);  

static int __init
int3_exception_notify(struct notifier_block *self, unsigned long val, void *data)
{
	unsigned long selftest = (unsigned long)&int3_selftest_ip;
	struct die_args *args = data;
	struct pt_regs *regs = args->regs;

	OPTIMIZER_HIDE_VAR(selftest);

	if (!regs || user_mode(regs))
		return NOTIFY_DONE;

	if (val != DIE_INT3)
		return NOTIFY_DONE;

	if (regs->ip - INT3_INSN_SIZE != selftest)
		return NOTIFY_DONE;

	int3_emulate_call(regs, (unsigned long)&int3_magic);
	return NOTIFY_STOP;
}

 
static noinline void __init int3_selftest(void)
{
	static __initdata struct notifier_block int3_exception_nb = {
		.notifier_call	= int3_exception_notify,
		.priority	= INT_MAX-1,  
	};
	unsigned int val = 0;

	BUG_ON(register_die_notifier(&int3_exception_nb));

	 
	asm volatile ("int3_selftest_ip:\n\t"
		      ANNOTATE_NOENDBR
		      "    int3; nop; nop; nop; nop\n\t"
		      : ASM_CALL_CONSTRAINT
		      : __ASM_SEL_RAW(a, D) (&val)
		      : "memory");

	BUG_ON(val != 1);

	unregister_die_notifier(&int3_exception_nb);
}

void __init alternative_instructions(void)
{
	int3_selftest();

	 
	stop_nmi();

	 

	 
	paravirt_set_cap();

	 
	apply_paravirt(__parainstructions, __parainstructions_end);

	 
	apply_retpolines(__retpoline_sites, __retpoline_sites_end);
	apply_returns(__return_sites, __return_sites_end);

	 
	apply_alternatives(__alt_instructions, __alt_instructions_end);

	apply_ibt_endbr(__ibt_endbr_seal, __ibt_endbr_seal_end);


	restart_nmi();
	alternatives_patched = 1;
}

 
void __init_or_module text_poke_early(void *addr, const void *opcode,
				      size_t len)
{
	unsigned long flags;

	if (boot_cpu_has(X86_FEATURE_NX) &&
	    is_module_text_address((unsigned long)addr)) {
		 
		memcpy(addr, opcode, len);
	} else {
		local_irq_save(flags);
		memcpy(addr, opcode, len);
		local_irq_restore(flags);
		sync_core();

		 
	}
}

typedef struct {
	struct mm_struct *mm;
} temp_mm_state_t;

 
static inline temp_mm_state_t use_temporary_mm(struct mm_struct *mm)
{
	temp_mm_state_t temp_state;

	lockdep_assert_irqs_disabled();

	 
	if (this_cpu_read(cpu_tlbstate_shared.is_lazy))
		leave_mm(smp_processor_id());

	temp_state.mm = this_cpu_read(cpu_tlbstate.loaded_mm);
	switch_mm_irqs_off(NULL, mm, current);

	 
	if (hw_breakpoint_active())
		hw_breakpoint_disable();

	return temp_state;
}

static inline void unuse_temporary_mm(temp_mm_state_t prev_state)
{
	lockdep_assert_irqs_disabled();
	switch_mm_irqs_off(NULL, prev_state.mm, current);

	 
	if (hw_breakpoint_active())
		hw_breakpoint_restore();
}

__ro_after_init struct mm_struct *poking_mm;
__ro_after_init unsigned long poking_addr;

static void text_poke_memcpy(void *dst, const void *src, size_t len)
{
	memcpy(dst, src, len);
}

static void text_poke_memset(void *dst, const void *src, size_t len)
{
	int c = *(const int *)src;

	memset(dst, c, len);
}

typedef void text_poke_f(void *dst, const void *src, size_t len);

static void *__text_poke(text_poke_f func, void *addr, const void *src, size_t len)
{
	bool cross_page_boundary = offset_in_page(addr) + len > PAGE_SIZE;
	struct page *pages[2] = {NULL};
	temp_mm_state_t prev;
	unsigned long flags;
	pte_t pte, *ptep;
	spinlock_t *ptl;
	pgprot_t pgprot;

	 
	BUG_ON(!after_bootmem);

	if (!core_kernel_text((unsigned long)addr)) {
		pages[0] = vmalloc_to_page(addr);
		if (cross_page_boundary)
			pages[1] = vmalloc_to_page(addr + PAGE_SIZE);
	} else {
		pages[0] = virt_to_page(addr);
		WARN_ON(!PageReserved(pages[0]));
		if (cross_page_boundary)
			pages[1] = virt_to_page(addr + PAGE_SIZE);
	}
	 
	BUG_ON(!pages[0] || (cross_page_boundary && !pages[1]));

	 
	pgprot = __pgprot(pgprot_val(PAGE_KERNEL) & ~_PAGE_GLOBAL);

	 
	ptep = get_locked_pte(poking_mm, poking_addr, &ptl);

	 
	VM_BUG_ON(!ptep);

	local_irq_save(flags);

	pte = mk_pte(pages[0], pgprot);
	set_pte_at(poking_mm, poking_addr, ptep, pte);

	if (cross_page_boundary) {
		pte = mk_pte(pages[1], pgprot);
		set_pte_at(poking_mm, poking_addr + PAGE_SIZE, ptep + 1, pte);
	}

	 
	prev = use_temporary_mm(poking_mm);

	kasan_disable_current();
	func((u8 *)poking_addr + offset_in_page(addr), src, len);
	kasan_enable_current();

	 
	barrier();

	pte_clear(poking_mm, poking_addr, ptep);
	if (cross_page_boundary)
		pte_clear(poking_mm, poking_addr + PAGE_SIZE, ptep + 1);

	 
	unuse_temporary_mm(prev);

	 
	flush_tlb_mm_range(poking_mm, poking_addr, poking_addr +
			   (cross_page_boundary ? 2 : 1) * PAGE_SIZE,
			   PAGE_SHIFT, false);

	if (func == text_poke_memcpy) {
		 
		BUG_ON(memcmp(addr, src, len));
	}

	local_irq_restore(flags);
	pte_unmap_unlock(ptep, ptl);
	return addr;
}

 
void *text_poke(void *addr, const void *opcode, size_t len)
{
	lockdep_assert_held(&text_mutex);

	return __text_poke(text_poke_memcpy, addr, opcode, len);
}

 
void *text_poke_kgdb(void *addr, const void *opcode, size_t len)
{
	return __text_poke(text_poke_memcpy, addr, opcode, len);
}

 
void *text_poke_copy(void *addr, const void *opcode, size_t len)
{
	unsigned long start = (unsigned long)addr;
	size_t patched = 0;

	if (WARN_ON_ONCE(core_kernel_text(start)))
		return NULL;

	mutex_lock(&text_mutex);
	while (patched < len) {
		unsigned long ptr = start + patched;
		size_t s;

		s = min_t(size_t, PAGE_SIZE * 2 - offset_in_page(ptr), len - patched);

		__text_poke(text_poke_memcpy, (void *)ptr, opcode + patched, s);
		patched += s;
	}
	mutex_unlock(&text_mutex);
	return addr;
}

 
void *text_poke_set(void *addr, int c, size_t len)
{
	unsigned long start = (unsigned long)addr;
	size_t patched = 0;

	if (WARN_ON_ONCE(core_kernel_text(start)))
		return NULL;

	mutex_lock(&text_mutex);
	while (patched < len) {
		unsigned long ptr = start + patched;
		size_t s;

		s = min_t(size_t, PAGE_SIZE * 2 - offset_in_page(ptr), len - patched);

		__text_poke(text_poke_memset, (void *)ptr, (void *)&c, s);
		patched += s;
	}
	mutex_unlock(&text_mutex);
	return addr;
}

static void do_sync_core(void *info)
{
	sync_core();
}

void text_poke_sync(void)
{
	on_each_cpu(do_sync_core, NULL, 1);
}

struct text_poke_loc {
	 
	s32 rel_addr;
	s32 disp;
	u8 len;
	u8 opcode;
	const u8 text[POKE_MAX_OPCODE_SIZE];
	 
	u8 old;
};

struct bp_patching_desc {
	struct text_poke_loc *vec;
	int nr_entries;
	atomic_t refs;
};

static struct bp_patching_desc *bp_desc;

static __always_inline
struct bp_patching_desc *try_get_desc(struct bp_patching_desc **descp)
{
	 
	struct bp_patching_desc *desc = __READ_ONCE(*descp);

	if (!desc || !arch_atomic_inc_not_zero(&desc->refs))
		return NULL;

	return desc;
}

static __always_inline void put_desc(struct bp_patching_desc *desc)
{
	smp_mb__before_atomic();
	arch_atomic_dec(&desc->refs);
}

static __always_inline void *text_poke_addr(struct text_poke_loc *tp)
{
	return _stext + tp->rel_addr;
}

static __always_inline int patch_cmp(const void *key, const void *elt)
{
	struct text_poke_loc *tp = (struct text_poke_loc *) elt;

	if (key < text_poke_addr(tp))
		return -1;
	if (key > text_poke_addr(tp))
		return 1;
	return 0;
}

noinstr int poke_int3_handler(struct pt_regs *regs)
{
	struct bp_patching_desc *desc;
	struct text_poke_loc *tp;
	int ret = 0;
	void *ip;

	if (user_mode(regs))
		return 0;

	 
	smp_rmb();

	desc = try_get_desc(&bp_desc);
	if (!desc)
		return 0;

	 
	ip = (void *) regs->ip - INT3_INSN_SIZE;

	 
	if (unlikely(desc->nr_entries > 1)) {
		tp = __inline_bsearch(ip, desc->vec, desc->nr_entries,
				      sizeof(struct text_poke_loc),
				      patch_cmp);
		if (!tp)
			goto out_put;
	} else {
		tp = desc->vec;
		if (text_poke_addr(tp) != ip)
			goto out_put;
	}

	ip += tp->len;

	switch (tp->opcode) {
	case INT3_INSN_OPCODE:
		 
		goto out_put;

	case RET_INSN_OPCODE:
		int3_emulate_ret(regs);
		break;

	case CALL_INSN_OPCODE:
		int3_emulate_call(regs, (long)ip + tp->disp);
		break;

	case JMP32_INSN_OPCODE:
	case JMP8_INSN_OPCODE:
		int3_emulate_jmp(regs, (long)ip + tp->disp);
		break;

	default:
		BUG();
	}

	ret = 1;

out_put:
	put_desc(desc);
	return ret;
}

#define TP_VEC_MAX (PAGE_SIZE / sizeof(struct text_poke_loc))
static struct text_poke_loc tp_vec[TP_VEC_MAX];
static int tp_vec_nr;

 
static void text_poke_bp_batch(struct text_poke_loc *tp, unsigned int nr_entries)
{
	struct bp_patching_desc desc = {
		.vec = tp,
		.nr_entries = nr_entries,
		.refs = ATOMIC_INIT(1),
	};
	unsigned char int3 = INT3_INSN_OPCODE;
	unsigned int i;
	int do_sync;

	lockdep_assert_held(&text_mutex);

	smp_store_release(&bp_desc, &desc);  

	 
	smp_wmb();

	 
	for (i = 0; i < nr_entries; i++) {
		tp[i].old = *(u8 *)text_poke_addr(&tp[i]);
		text_poke(text_poke_addr(&tp[i]), &int3, INT3_INSN_SIZE);
	}

	text_poke_sync();

	 
	for (do_sync = 0, i = 0; i < nr_entries; i++) {
		u8 old[POKE_MAX_OPCODE_SIZE] = { tp[i].old, };
		int len = tp[i].len;

		if (len - INT3_INSN_SIZE > 0) {
			memcpy(old + INT3_INSN_SIZE,
			       text_poke_addr(&tp[i]) + INT3_INSN_SIZE,
			       len - INT3_INSN_SIZE);
			text_poke(text_poke_addr(&tp[i]) + INT3_INSN_SIZE,
				  (const char *)tp[i].text + INT3_INSN_SIZE,
				  len - INT3_INSN_SIZE);
			do_sync++;
		}

		 
		perf_event_text_poke(text_poke_addr(&tp[i]), old, len,
				     tp[i].text, len);
	}

	if (do_sync) {
		 
		text_poke_sync();
	}

	 
	for (do_sync = 0, i = 0; i < nr_entries; i++) {
		if (tp[i].text[0] == INT3_INSN_OPCODE)
			continue;

		text_poke(text_poke_addr(&tp[i]), tp[i].text, INT3_INSN_SIZE);
		do_sync++;
	}

	if (do_sync)
		text_poke_sync();

	 
	WRITE_ONCE(bp_desc, NULL);  
	if (!atomic_dec_and_test(&desc.refs))
		atomic_cond_read_acquire(&desc.refs, !VAL);
}

static void text_poke_loc_init(struct text_poke_loc *tp, void *addr,
			       const void *opcode, size_t len, const void *emulate)
{
	struct insn insn;
	int ret, i;

	memcpy((void *)tp->text, opcode, len);
	if (!emulate)
		emulate = opcode;

	ret = insn_decode_kernel(&insn, emulate);
	BUG_ON(ret < 0);

	tp->rel_addr = addr - (void *)_stext;
	tp->len = len;
	tp->opcode = insn.opcode.bytes[0];

	switch (tp->opcode) {
	case RET_INSN_OPCODE:
	case JMP32_INSN_OPCODE:
	case JMP8_INSN_OPCODE:
		 
		for (i = insn.length; i < len; i++)
			BUG_ON(tp->text[i] != INT3_INSN_OPCODE);
		break;

	default:
		BUG_ON(len != insn.length);
	};


	switch (tp->opcode) {
	case INT3_INSN_OPCODE:
	case RET_INSN_OPCODE:
		break;

	case CALL_INSN_OPCODE:
	case JMP32_INSN_OPCODE:
	case JMP8_INSN_OPCODE:
		tp->disp = insn.immediate.value;
		break;

	default:  
		switch (len) {
		case 2:  
			BUG_ON(memcmp(emulate, x86_nops[len], len));
			tp->opcode = JMP8_INSN_OPCODE;
			tp->disp = 0;
			break;

		case 5:  
			BUG_ON(memcmp(emulate, x86_nops[len], len));
			tp->opcode = JMP32_INSN_OPCODE;
			tp->disp = 0;
			break;

		default:  
			BUG();
		}
		break;
	}
}

 
static bool tp_order_fail(void *addr)
{
	struct text_poke_loc *tp;

	if (!tp_vec_nr)
		return false;

	if (!addr)  
		return true;

	tp = &tp_vec[tp_vec_nr - 1];
	if ((unsigned long)text_poke_addr(tp) > (unsigned long)addr)
		return true;

	return false;
}

static void text_poke_flush(void *addr)
{
	if (tp_vec_nr == TP_VEC_MAX || tp_order_fail(addr)) {
		text_poke_bp_batch(tp_vec, tp_vec_nr);
		tp_vec_nr = 0;
	}
}

void text_poke_finish(void)
{
	text_poke_flush(NULL);
}

void __ref text_poke_queue(void *addr, const void *opcode, size_t len, const void *emulate)
{
	struct text_poke_loc *tp;

	if (unlikely(system_state == SYSTEM_BOOTING)) {
		text_poke_early(addr, opcode, len);
		return;
	}

	text_poke_flush(addr);

	tp = &tp_vec[tp_vec_nr++];
	text_poke_loc_init(tp, addr, opcode, len, emulate);
}

 
void __ref text_poke_bp(void *addr, const void *opcode, size_t len, const void *emulate)
{
	struct text_poke_loc tp;

	if (unlikely(system_state == SYSTEM_BOOTING)) {
		text_poke_early(addr, opcode, len);
		return;
	}

	text_poke_loc_init(&tp, addr, opcode, len, emulate);
	text_poke_bp_batch(&tp, 1);
}
