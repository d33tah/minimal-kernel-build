 
 

#include "misc.h"
#include "error.h"
#include "pgtable.h"
#include "../string.h"
#include "../voffset.h"
#include <asm/bootparam_utils.h>

 

 
#define STATIC		static
 
#define MALLOC_VISIBLE
#include <linux/decompress/mm.h>

 
#define memzero(s, n)	memset((s), 0, (n))
#ifndef memmove
#define memmove		memmove
 
void *memmove(void *dest, const void *src, size_t n);
#endif

 
struct boot_params *boot_params;

struct port_io_ops pio_ops;

memptr free_mem_ptr;
memptr free_mem_end_ptr;

static char *vidmem;
static int vidport;

 
static int lines __section(".data");
static int cols __section(".data");




#include "../../../../lib/decompress_unxz.c"



 

static void scroll(void)
{
	int i;

	memmove(vidmem, vidmem + cols * 2, (lines - 1) * cols * 2);
	for (i = (lines - 1) * cols * 2; i < lines * cols * 2; i += 2)
		vidmem[i] = ' ';
}

#define XMTRDY          0x20

#define TXR             0        
#define LSR             5        
static void serial_putchar(int ch)
{
	unsigned timeout = 0xffff;

	while ((inb(early_serial_base + LSR) & XMTRDY) == 0 && --timeout)
		cpu_relax();

	outb(ch, early_serial_base + TXR);
}

void __putstr(const char *s)
{
	int x, y, pos;
	char c;

	if (early_serial_base) {
		const char *str = s;
		while (*str) {
			if (*str == '\n')
				serial_putchar('\r');
			serial_putchar(*str++);
		}
	}

	if (lines == 0 || cols == 0)
		return;

	x = boot_params->screen_info.orig_x;
	y = boot_params->screen_info.orig_y;

	while ((c = *s++) != '\0') {
		if (c == '\n') {
			x = 0;
			if (++y >= lines) {
				scroll();
				y--;
			}
		} else {
			vidmem[(x + cols * y) * 2] = c;
			if (++x >= cols) {
				x = 0;
				if (++y >= lines) {
					scroll();
					y--;
				}
			}
		}
	}

	boot_params->screen_info.orig_x = x;
	boot_params->screen_info.orig_y = y;

	pos = (x + cols * y) * 2;	 
	outb(14, vidport);
	outb(0xff & (pos >> 9), vidport+1);
	outb(15, vidport);
	outb(0xff & (pos >> 1), vidport+1);
}

void __puthex(unsigned long value)
{
	char alpha[2] = "0";
	int bits;

	for (bits = sizeof(value) * 8 - 4; bits >= 0; bits -= 4) {
		unsigned long digit = (value >> bits) & 0xf;

		if (digit < 0xA)
			alpha[0] = '0' + digit;
		else
			alpha[0] = 'a' + (digit - 0xA);

		__putstr(alpha);
	}
}

static inline void handle_relocations(void *output, unsigned long output_len,
				      unsigned long virt_addr)
{ }

static void parse_elf(void *output)
{
	Elf32_Ehdr ehdr;
	Elf32_Phdr *phdrs, *phdr;
	void *dest;
	int i;

	memcpy(&ehdr, output, sizeof(ehdr));
	if (ehdr.e_ident[EI_MAG0] != ELFMAG0 ||
	   ehdr.e_ident[EI_MAG1] != ELFMAG1 ||
	   ehdr.e_ident[EI_MAG2] != ELFMAG2 ||
	   ehdr.e_ident[EI_MAG3] != ELFMAG3) {
		error("Kernel is not a valid ELF file");
		return;
	}

	debug_putstr("Parsing ELF... ");

	phdrs = malloc(sizeof(*phdrs) * ehdr.e_phnum);
	if (!phdrs)
		error("Failed to allocate space for phdrs");

	memcpy(phdrs, output + ehdr.e_phoff, sizeof(*phdrs) * ehdr.e_phnum);

	for (i = 0; i < ehdr.e_phnum; i++) {
		phdr = &phdrs[i];

		switch (phdr->p_type) {
		case PT_LOAD:
			dest = (void *)(phdr->p_paddr);
			memmove(dest, output + phdr->p_offset, phdr->p_filesz);
			break;
		default:   break;
		}
	}

	free(phdrs);
}

 
asmlinkage __visible void *extract_kernel(void *rmode, memptr heap,
				  unsigned char *input_data,
				  unsigned long input_len,
				  unsigned char *output,
				  unsigned long output_len)
{
	const unsigned long kernel_total_size = VO__end - VO__text;
	unsigned long virt_addr = LOAD_PHYSICAL_ADDR;
	unsigned long needed_size;

	 
	boot_params = rmode;

	 
	boot_params->hdr.loadflags &= ~KASLR_FLAG;

	sanitize_boot_params(boot_params);

	if (boot_params->screen_info.orig_video_mode == 7) {
		vidmem = (char *) 0xb0000;
		vidport = 0x3b4;
	} else {
		vidmem = (char *) 0xb8000;
		vidport = 0x3d4;
	}

	lines = boot_params->screen_info.orig_video_lines;
	cols = boot_params->screen_info.orig_video_cols;

	init_default_io_ops();

	 
	early_tdx_detect();

	console_init();

	 
	boot_params->acpi_rsdp_addr = get_rsdp_addr();

	debug_putstr("early console in extract_kernel\n");

	free_mem_ptr     = heap;	 
	free_mem_end_ptr = heap + BOOT_HEAP_SIZE;

	 
	needed_size = max(output_len, kernel_total_size);

	 
	debug_putaddr(input_data);
	debug_putaddr(input_len);
	debug_putaddr(output);
	debug_putaddr(output_len);
	debug_putaddr(kernel_total_size);
	debug_putaddr(needed_size);


	choose_random_location((unsigned long)input_data, input_len,
				(unsigned long *)&output,
				needed_size,
				&virt_addr);

	 
	if ((unsigned long)output & (MIN_KERNEL_ALIGN - 1))
		error("Destination physical address inappropriately aligned");
	if (virt_addr & (MIN_KERNEL_ALIGN - 1))
		error("Destination virtual address inappropriately aligned");
	if (heap > ((-__PAGE_OFFSET-(128<<20)-1) & 0x7fffffff))
		error("Destination address too large");
	if (virt_addr != LOAD_PHYSICAL_ADDR)
		error("Destination virtual address changed when not relocatable");

	debug_putstr("\nDecompressing Linux... ");
	__decompress(input_data, input_len, NULL, NULL, output, output_len,
			NULL, error);
	parse_elf(output);
	handle_relocations(output, output_len, virt_addr);
	debug_putstr("done.\nBooting the kernel.\n");

	 
	cleanup_exception_handling();

	return output;
}

void fortify_panic(const char *name)
{
	error("detected buffer overflow");
}
