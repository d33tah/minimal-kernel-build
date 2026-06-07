
#include <linux/module.h>
#include <linux/mman.h>
#include <linux/binfmts.h>
#include <linux/ptrace.h>
#include <linux/pagemap.h>
#include <linux/sched/task_stack.h>

static int load_elf_binary(struct linux_binprm *bprm);

static struct linux_binfmt elf_format = {
	.module = THIS_MODULE,
	.load_binary = load_elf_binary,
};

/*
 * The decompressor stub halts before the kernel proper ever runs, so the
 * ELF loader success path is never exercised. Keep the format registered so
 * the boot call graph links, but reduce the loader to a stub.
 */
static int load_elf_binary(struct linux_binprm *bprm)
{
	return -ENOEXEC;
}

static int __init init_elf_binfmt(void)
{
	register_binfmt(&elf_format);
	return 0;
}

core_initcall(init_elf_binfmt);
MODULE_LICENSE("GPL");
