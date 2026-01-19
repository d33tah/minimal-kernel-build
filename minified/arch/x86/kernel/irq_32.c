
/* seq_file.h removed - header is empty */
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/kernel_stat.h>
#include <linux/cpu.h>
#include <linux/delay.h>
#include <linux/uaccess.h>
#include <linux/percpu.h>
#include <linux/mm.h>

#include <asm/apic.h>
#include <asm/nospec-branch.h>
void do_softirq_own_stack(void);

/* check_stack_overflow and print_stack_overflow removed - always 0/empty */

DEFINE_PER_CPU(struct irq_stack *, hardirq_stack_ptr);
DEFINE_PER_CPU(struct irq_stack *, softirq_stack_ptr);

static void call_on_stack(void *func, void *stack)
{
	asm volatile("xchgl	%%ebx,%%esp	\n" CALL_NOSPEC
		     "movl	%%ebx,%%esp	\n"
		     : "=b"(stack)
		     : "0"(stack), [thunk_target] "D"(func)
		     : "memory", "cc", "edx", "ecx", "eax");
}

static inline void *current_stack(void)
{
	return (void *)(current_stack_pointer & ~(THREAD_SIZE - 1));
}

static inline int execute_on_irq_stack(struct irq_desc *desc)
{
	struct irq_stack *curstk, *irqstk;
	u32 *isp, *prev_esp, arg1;

	curstk = (struct irq_stack *)current_stack();
	irqstk = __this_cpu_read(hardirq_stack_ptr);

	if (unlikely(curstk == irqstk))
		return 0;

	isp = (u32 *)((char *)irqstk + sizeof(*irqstk));

	prev_esp = (u32 *)irqstk;
	*prev_esp = current_stack_pointer;

	/* overflow check removed - always 0 */

	asm volatile("xchgl	%%ebx,%%esp	\n" CALL_NOSPEC
		     "movl	%%ebx,%%esp	\n"
		     : "=a"(arg1), "=b"(isp)
		     : "0"(desc), "1"(isp), [thunk_target] "D"(desc->handle_irq)
		     : "memory", "cc", "ecx");
	return 1;
}

int irq_init_percpu_irqstack(unsigned int cpu)
{
	int node = cpu_to_node(cpu);
	struct page *ph, *ps;

	if (per_cpu(hardirq_stack_ptr, cpu))
		return 0;

	ph = alloc_pages_node(node, THREADINFO_GFP, THREAD_SIZE_ORDER);
	if (!ph)
		return -ENOMEM;
	ps = alloc_pages_node(node, THREADINFO_GFP, THREAD_SIZE_ORDER);
	if (!ps) {
		__free_pages(ph, THREAD_SIZE_ORDER);
		return -ENOMEM;
	}

	per_cpu(hardirq_stack_ptr, cpu) = page_address(ph);
	per_cpu(softirq_stack_ptr, cpu) = page_address(ps);
	return 0;
}

void do_softirq_own_stack(void)
{
	struct irq_stack *irqstk;
	u32 *isp, *prev_esp;

	irqstk = __this_cpu_read(softirq_stack_ptr);

	isp = (u32 *)((char *)irqstk + sizeof(*irqstk));

	prev_esp = (u32 *)irqstk;
	*prev_esp = current_stack_pointer;

	call_on_stack(__do_softirq, isp);
}

void __handle_irq(struct irq_desc *desc, struct pt_regs *regs)
{
	/* overflow checks removed - check_stack_overflow always returned 0 */
	if (user_mode(regs) || !execute_on_irq_stack(desc))
		generic_handle_irq_desc(desc);
}
