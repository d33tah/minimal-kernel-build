#include <asm/syscall.h>

#define __SYSCALL(nr, sym) extern long __ia32_##sym(const struct pt_regs *);
#include <asm/syscalls_32.h>
#undef __SYSCALL

#define __SYSCALL(nr, sym) __ia32_##sym,
__visible const sys_call_ptr_t ia32_sys_call_table[] = {
#include <asm/syscalls_32.h>
};
