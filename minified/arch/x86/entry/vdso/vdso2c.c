
#include <inttypes.h>
#include <stdint.h>
#include <unistd.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <err.h>

#include <sys/mman.h>
#include <sys/types.h>

#include <tools/le_byteshift.h>

#include <linux/elf.h>
#include <linux/types.h>
#include <linux/kernel.h>

const char *outfilename;

enum {
	sym_vvar_start,
	sym_vvar_page,
	sym_pvclock_page,
	sym_hvclock_page,
	sym_timens_page,
};

const int special_pages[] = {
	sym_vvar_page,
	sym_pvclock_page,
	sym_hvclock_page,
	sym_timens_page,
};

struct vdso_sym {
	const char *name;
	bool export;
};

struct vdso_sym required_syms[] = {
	[sym_vvar_start] = { "vvar_start", true },
	[sym_vvar_page] = { "vvar_page", true },
	[sym_pvclock_page] = { "pvclock_page", true },
	[sym_hvclock_page] = { "hvclock_page", true },
	[sym_timens_page] = { "timens_page", true },
	{ "VDSO32_NOTE_MASK", true },
	{ "__kernel_vsyscall", true },
	{ "__kernel_sigreturn", true },
	{ "__kernel_rt_sigreturn", true },
	{ "int80_landing_pad", true },
	{ "vdso32_rt_sigreturn_landing_pad", true },
	{ "vdso32_sigreturn_landing_pad", true },
};

__attribute__((format(printf, 1, 2))) __attribute__((noreturn)) static void
fail(const char *format, ...)
{
	va_list ap;
	va_start(ap, format);
	fprintf(stderr, "Error: ");
	vfprintf(stderr, format, ap);
	if (outfilename)
		unlink(outfilename);
	exit(1);
	va_end(ap);
}

#define GLE(x, bits, ifnot)                                                \
	__builtin_choose_expr((sizeof(*(x)) == bits / 8),                  \
			      (__typeof__(*(x)))get_unaligned_le##bits(x), \
			      ifnot)

extern void bad_get_le(void);
#define LAST_GLE(x) __builtin_choose_expr(sizeof(*(x)) == 1, *(x), bad_get_le())

#define GET_LE(x) GLE(x, 64, GLE(x, 32, GLE(x, 16, LAST_GLE(x))))

#define PLE(x, val, bits, ifnot)                          \
	__builtin_choose_expr((sizeof(*(x)) == bits / 8), \
			      put_unaligned_le##bits((val), (x)), ifnot)

extern void bad_put_le(void);
#define LAST_PLE(x, val) \
	__builtin_choose_expr(sizeof(*(x)) == 1, *(x) = (val), bad_put_le())

#define PUT_LE(x, val) \
	PLE(x, val, 64, PLE(x, val, 32, PLE(x, val, 16, LAST_PLE(x, val))))

#define NSYMS ARRAY_SIZE(required_syms)

#define BITSFUNC3(name, bits, suffix) name##bits##suffix
#define BITSFUNC2(name, bits, suffix) BITSFUNC3(name, bits, suffix)
#define BITSFUNC(name) BITSFUNC2(name, ELF_BITS, )

#define INT_BITS BITSFUNC2(int, ELF_BITS, _t)

#define ELF_BITS_XFORM2(bits, x) Elf##bits##_##x
#define ELF_BITS_XFORM(bits, x) ELF_BITS_XFORM2(bits, x)
#define ELF(x) ELF_BITS_XFORM(ELF_BITS, x)

#define ELF_BITS 64

static void BITSFUNC(copy)(FILE *outfile, const unsigned char *data, size_t len)
{
	size_t i;

	for (i = 0; i < len; i++) {
		if (i % 10 == 0)
			fprintf(outfile, "\n\t");
		fprintf(outfile, "0x%02X, ", (int)(data)[i]);
	}
}

static void BITSFUNC(extract)(const unsigned char *data, size_t data_len,
			      FILE *outfile, ELF(Shdr) * sec, const char *name)
{
	unsigned long offset;
	size_t len;

	offset = (unsigned long)GET_LE(&sec->sh_offset);
	len = (size_t)GET_LE(&sec->sh_size);

	if (offset + len > data_len)
		fail("section to extract overruns input data");

	fprintf(outfile, "static const unsigned char %s[%zu] = {", name, len);
	BITSFUNC(copy)(outfile, data + offset, len);
	fprintf(outfile, "\n};\n\n");
}

static void BITSFUNC(go)(void *raw_addr, size_t raw_len, void *stripped_addr,
			 size_t stripped_len, FILE *outfile,
			 const char *image_name)
{
	int found_load = 0;
	unsigned long load_size = -1;
	unsigned long mapping_size;
	ELF(Ehdr) *hdr = (ELF(Ehdr) *)raw_addr;
	unsigned long i, syms_nr;
	ELF(Shdr) *symtab_hdr = NULL, *strtab_hdr, *secstrings_hdr,
		  *alt_sec = NULL, *extable_sec = NULL;
	ELF(Dyn) *dyn = 0, *dyn_end = 0;
	const char *secstrings;
	INT_BITS syms[NSYMS] = {};

	ELF(Phdr) *pt = (ELF(Phdr) *)(raw_addr + GET_LE(&hdr->e_phoff));

	if (GET_LE(&hdr->e_type) != ET_DYN)
		fail("input is not a shared object\n");

	for (i = 0; i < GET_LE(&hdr->e_phnum); i++) {
		if (GET_LE(&pt[i].p_type) == PT_LOAD) {
			if (found_load)
				fail("multiple PT_LOAD segs\n");

			if (GET_LE(&pt[i].p_offset) != 0 ||
			    GET_LE(&pt[i].p_vaddr) != 0)
				fail("PT_LOAD in wrong place\n");

			if (GET_LE(&pt[i].p_memsz) != GET_LE(&pt[i].p_filesz))
				fail("cannot handle memsz != filesz\n");

			load_size = GET_LE(&pt[i].p_memsz);
			found_load = 1;
		} else if (GET_LE(&pt[i].p_type) == PT_DYNAMIC) {
			dyn = raw_addr + GET_LE(&pt[i].p_offset);
			dyn_end = raw_addr + GET_LE(&pt[i].p_offset) +
				  GET_LE(&pt[i].p_memsz);
		}
	}
	if (!found_load)
		fail("no PT_LOAD seg\n");

	if (stripped_len < load_size)
		fail("stripped input is too short\n");

	if (!dyn)
		fail("input has no PT_DYNAMIC section -- your toolchain is buggy\n");

	for (i = 0; dyn + i < dyn_end && GET_LE(&dyn[i].d_tag) != DT_NULL;
	     i++) {
		typeof(dyn[i].d_tag) tag = GET_LE(&dyn[i].d_tag);
		if (tag == DT_REL || tag == DT_RELSZ || tag == DT_RELA ||
		    tag == DT_RELENT || tag == DT_TEXTREL)
			fail("vdso image contains dynamic relocations\n");
	}

	secstrings_hdr = raw_addr + GET_LE(&hdr->e_shoff) +
			 GET_LE(&hdr->e_shentsize) * GET_LE(&hdr->e_shstrndx);
	secstrings = raw_addr + GET_LE(&secstrings_hdr->sh_offset);
	for (i = 0; i < GET_LE(&hdr->e_shnum); i++) {
		ELF(Shdr) *sh = raw_addr + GET_LE(&hdr->e_shoff) +
				GET_LE(&hdr->e_shentsize) * i;
		if (GET_LE(&sh->sh_type) == SHT_SYMTAB)
			symtab_hdr = sh;

		if (!strcmp(secstrings + GET_LE(&sh->sh_name),
			    ".altinstructions"))
			alt_sec = sh;
		if (!strcmp(secstrings + GET_LE(&sh->sh_name), "__ex_table"))
			extable_sec = sh;
	}

	if (!symtab_hdr)
		fail("no symbol table\n");

	strtab_hdr = raw_addr + GET_LE(&hdr->e_shoff) +
		     GET_LE(&hdr->e_shentsize) * GET_LE(&symtab_hdr->sh_link);

	syms_nr =
		GET_LE(&symtab_hdr->sh_size) / GET_LE(&symtab_hdr->sh_entsize);

	for (i = 0; i < syms_nr; i++) {
		unsigned int k;
		ELF(Sym) *sym = raw_addr + GET_LE(&symtab_hdr->sh_offset) +
				GET_LE(&symtab_hdr->sh_entsize) * i;
		const char *sym_name = raw_addr +
				       GET_LE(&strtab_hdr->sh_offset) +
				       GET_LE(&sym->st_name);

		for (k = 0; k < NSYMS; k++) {
			if (!strcmp(sym_name, required_syms[k].name)) {
				if (syms[k]) {
					fail("duplicate symbol %s\n",
					     required_syms[k].name);
				}

				syms[k] = GET_LE(&sym->st_value);
			}
		}
	}

	for (i = 0; i < sizeof(special_pages) / sizeof(special_pages[0]); i++) {
		INT_BITS symval = syms[special_pages[i]];

		if (!symval)
			continue;

		if (symval % 4096)
			fail("%s must be a multiple of 4096\n",
			     required_syms[i].name);
		if (symval + 4096 < syms[sym_vvar_start])
			fail("%s underruns vvar_start\n",
			     required_syms[i].name);
		if (symval + 4096 > 0)
			fail("%s is on the wrong side of the vdso text\n",
			     required_syms[i].name);
	}
	if (syms[sym_vvar_start] % 4096)
		fail("vvar_begin must be a multiple of 4096\n");

	if (!image_name) {
		fwrite(stripped_addr, stripped_len, 1, outfile);
		return;
	}

	mapping_size = (stripped_len + 4095) / 4096 * 4096;

	fprintf(outfile, "/* AUTOMATICALLY GENERATED -- DO NOT EDIT */\n\n");
	fprintf(outfile, "#include <linux/linkage.h>\n");
	fprintf(outfile, "#include <asm/page_types.h>\n");
	fprintf(outfile, "#include <asm/vdso.h>\n");
	fprintf(outfile, "\n");
	fprintf(outfile,
		"static unsigned char raw_data[%lu] __ro_after_init __aligned(PAGE_SIZE) = {",
		mapping_size);
	for (i = 0; i < stripped_len; i++) {
		if (i % 10 == 0)
			fprintf(outfile, "\n\t");
		fprintf(outfile, "0x%02X, ",
			(int)((unsigned char *)stripped_addr)[i]);
	}
	fprintf(outfile, "\n};\n\n");
	if (extable_sec)
		BITSFUNC(extract)(raw_addr, raw_len, outfile, extable_sec,
				  "extable");

	fprintf(outfile, "const struct vdso_image %s = {\n", image_name);
	fprintf(outfile, "\t.data = raw_data,\n");
	fprintf(outfile, "\t.size = %lu,\n", mapping_size);
	if (alt_sec) {
		fprintf(outfile, "\t.alt = %lu,\n",
			(unsigned long)GET_LE(&alt_sec->sh_offset));
		fprintf(outfile, "\t.alt_len = %lu,\n",
			(unsigned long)GET_LE(&alt_sec->sh_size));
	}
	if (extable_sec) {
		fprintf(outfile, "\t.extable_base = %lu,\n",
			(unsigned long)GET_LE(&extable_sec->sh_offset));
		fprintf(outfile, "\t.extable_len = %lu,\n",
			(unsigned long)GET_LE(&extable_sec->sh_size));
		fprintf(outfile, "\t.extable = extable,\n");
	}

	for (i = 0; i < NSYMS; i++) {
		if (required_syms[i].export && syms[i])
			fprintf(outfile, "\t.sym_%s = %" PRIi64 ",\n",
				required_syms[i].name, (int64_t)syms[i]);
	}
	fprintf(outfile, "};\n");
}

#undef ELF_BITS

#define ELF_BITS 32

static void BITSFUNC(copy)(FILE *outfile, const unsigned char *data, size_t len)
{
	size_t i;

	for (i = 0; i < len; i++) {
		if (i % 10 == 0)
			fprintf(outfile, "\n\t");
		fprintf(outfile, "0x%02X, ", (int)(data)[i]);
	}
}

static void BITSFUNC(extract)(const unsigned char *data, size_t data_len,
			      FILE *outfile, ELF(Shdr) * sec, const char *name)
{
	unsigned long offset;
	size_t len;

	offset = (unsigned long)GET_LE(&sec->sh_offset);
	len = (size_t)GET_LE(&sec->sh_size);

	if (offset + len > data_len)
		fail("section to extract overruns input data");

	fprintf(outfile, "static const unsigned char %s[%zu] = {", name, len);
	BITSFUNC(copy)(outfile, data + offset, len);
	fprintf(outfile, "\n};\n\n");
}

static void BITSFUNC(go)(void *raw_addr, size_t raw_len, void *stripped_addr,
			 size_t stripped_len, FILE *outfile,
			 const char *image_name)
{
	int found_load = 0;
	unsigned long load_size = -1;
	unsigned long mapping_size;
	ELF(Ehdr) *hdr = (ELF(Ehdr) *)raw_addr;
	unsigned long i, syms_nr;
	ELF(Shdr) *symtab_hdr = NULL, *strtab_hdr, *secstrings_hdr,
		  *alt_sec = NULL, *extable_sec = NULL;
	ELF(Dyn) *dyn = 0, *dyn_end = 0;
	const char *secstrings;
	INT_BITS syms[NSYMS] = {};

	ELF(Phdr) *pt = (ELF(Phdr) *)(raw_addr + GET_LE(&hdr->e_phoff));

	if (GET_LE(&hdr->e_type) != ET_DYN)
		fail("input is not a shared object\n");

	for (i = 0; i < GET_LE(&hdr->e_phnum); i++) {
		if (GET_LE(&pt[i].p_type) == PT_LOAD) {
			if (found_load)
				fail("multiple PT_LOAD segs\n");

			if (GET_LE(&pt[i].p_offset) != 0 ||
			    GET_LE(&pt[i].p_vaddr) != 0)
				fail("PT_LOAD in wrong place\n");

			if (GET_LE(&pt[i].p_memsz) != GET_LE(&pt[i].p_filesz))
				fail("cannot handle memsz != filesz\n");

			load_size = GET_LE(&pt[i].p_memsz);
			found_load = 1;
		} else if (GET_LE(&pt[i].p_type) == PT_DYNAMIC) {
			dyn = raw_addr + GET_LE(&pt[i].p_offset);
			dyn_end = raw_addr + GET_LE(&pt[i].p_offset) +
				  GET_LE(&pt[i].p_memsz);
		}
	}
	if (!found_load)
		fail("no PT_LOAD seg\n");

	if (stripped_len < load_size)
		fail("stripped input is too short\n");

	if (!dyn)
		fail("input has no PT_DYNAMIC section -- your toolchain is buggy\n");

	for (i = 0; dyn + i < dyn_end && GET_LE(&dyn[i].d_tag) != DT_NULL;
	     i++) {
		typeof(dyn[i].d_tag) tag = GET_LE(&dyn[i].d_tag);
		if (tag == DT_REL || tag == DT_RELSZ || tag == DT_RELA ||
		    tag == DT_RELENT || tag == DT_TEXTREL)
			fail("vdso image contains dynamic relocations\n");
	}

	secstrings_hdr = raw_addr + GET_LE(&hdr->e_shoff) +
			 GET_LE(&hdr->e_shentsize) * GET_LE(&hdr->e_shstrndx);
	secstrings = raw_addr + GET_LE(&secstrings_hdr->sh_offset);
	for (i = 0; i < GET_LE(&hdr->e_shnum); i++) {
		ELF(Shdr) *sh = raw_addr + GET_LE(&hdr->e_shoff) +
				GET_LE(&hdr->e_shentsize) * i;
		if (GET_LE(&sh->sh_type) == SHT_SYMTAB)
			symtab_hdr = sh;

		if (!strcmp(secstrings + GET_LE(&sh->sh_name),
			    ".altinstructions"))
			alt_sec = sh;
		if (!strcmp(secstrings + GET_LE(&sh->sh_name), "__ex_table"))
			extable_sec = sh;
	}

	if (!symtab_hdr)
		fail("no symbol table\n");

	strtab_hdr = raw_addr + GET_LE(&hdr->e_shoff) +
		     GET_LE(&hdr->e_shentsize) * GET_LE(&symtab_hdr->sh_link);

	syms_nr =
		GET_LE(&symtab_hdr->sh_size) / GET_LE(&symtab_hdr->sh_entsize);

	for (i = 0; i < syms_nr; i++) {
		unsigned int k;
		ELF(Sym) *sym = raw_addr + GET_LE(&symtab_hdr->sh_offset) +
				GET_LE(&symtab_hdr->sh_entsize) * i;
		const char *sym_name = raw_addr +
				       GET_LE(&strtab_hdr->sh_offset) +
				       GET_LE(&sym->st_name);

		for (k = 0; k < NSYMS; k++) {
			if (!strcmp(sym_name, required_syms[k].name)) {
				if (syms[k]) {
					fail("duplicate symbol %s\n",
					     required_syms[k].name);
				}

				syms[k] = GET_LE(&sym->st_value);
			}
		}
	}

	for (i = 0; i < sizeof(special_pages) / sizeof(special_pages[0]); i++) {
		INT_BITS symval = syms[special_pages[i]];

		if (!symval)
			continue;

		if (symval % 4096)
			fail("%s must be a multiple of 4096\n",
			     required_syms[i].name);
		if (symval + 4096 < syms[sym_vvar_start])
			fail("%s underruns vvar_start\n",
			     required_syms[i].name);
		if (symval + 4096 > 0)
			fail("%s is on the wrong side of the vdso text\n",
			     required_syms[i].name);
	}
	if (syms[sym_vvar_start] % 4096)
		fail("vvar_begin must be a multiple of 4096\n");

	if (!image_name) {
		fwrite(stripped_addr, stripped_len, 1, outfile);
		return;
	}

	mapping_size = (stripped_len + 4095) / 4096 * 4096;

	fprintf(outfile, "/* AUTOMATICALLY GENERATED -- DO NOT EDIT */\n\n");
	fprintf(outfile, "#include <linux/linkage.h>\n");
	fprintf(outfile, "#include <asm/page_types.h>\n");
	fprintf(outfile, "#include <asm/vdso.h>\n");
	fprintf(outfile, "\n");
	fprintf(outfile,
		"static unsigned char raw_data[%lu] __ro_after_init __aligned(PAGE_SIZE) = {",
		mapping_size);
	for (i = 0; i < stripped_len; i++) {
		if (i % 10 == 0)
			fprintf(outfile, "\n\t");
		fprintf(outfile, "0x%02X, ",
			(int)((unsigned char *)stripped_addr)[i]);
	}
	fprintf(outfile, "\n};\n\n");
	if (extable_sec)
		BITSFUNC(extract)(raw_addr, raw_len, outfile, extable_sec,
				  "extable");

	fprintf(outfile, "const struct vdso_image %s = {\n", image_name);
	fprintf(outfile, "\t.data = raw_data,\n");
	fprintf(outfile, "\t.size = %lu,\n", mapping_size);
	if (alt_sec) {
		fprintf(outfile, "\t.alt = %lu,\n",
			(unsigned long)GET_LE(&alt_sec->sh_offset));
		fprintf(outfile, "\t.alt_len = %lu,\n",
			(unsigned long)GET_LE(&alt_sec->sh_size));
	}
	if (extable_sec) {
		fprintf(outfile, "\t.extable_base = %lu,\n",
			(unsigned long)GET_LE(&extable_sec->sh_offset));
		fprintf(outfile, "\t.extable_len = %lu,\n",
			(unsigned long)GET_LE(&extable_sec->sh_size));
		fprintf(outfile, "\t.extable = extable,\n");
	}

	for (i = 0; i < NSYMS; i++) {
		if (required_syms[i].export && syms[i])
			fprintf(outfile, "\t.sym_%s = %" PRIi64 ",\n",
				required_syms[i].name, (int64_t)syms[i]);
	}
	fprintf(outfile, "};\n");
}

#undef ELF_BITS

static void go(void *raw_addr, size_t raw_len, void *stripped_addr,
	       size_t stripped_len, FILE *outfile, const char *name)
{
	Elf64_Ehdr *hdr = (Elf64_Ehdr *)raw_addr;

	if (hdr->e_ident[EI_CLASS] == ELFCLASS64) {
		go64(raw_addr, raw_len, stripped_addr, stripped_len, outfile,
		     name);
	} else if (hdr->e_ident[EI_CLASS] == ELFCLASS32) {
		go32(raw_addr, raw_len, stripped_addr, stripped_len, outfile,
		     name);
	} else {
		fail("unknown ELF class\n");
	}
}

static void map_input(const char *name, void **addr, size_t *len, int prot)
{
	off_t tmp_len;

	int fd = open(name, O_RDONLY);
	if (fd == -1)
		err(1, "open(%s)", name);

	tmp_len = lseek(fd, 0, SEEK_END);
	if (tmp_len == (off_t)-1)
		err(1, "lseek");
	*len = (size_t)tmp_len;

	*addr = mmap(NULL, tmp_len, prot, MAP_PRIVATE, fd, 0);
	if (*addr == MAP_FAILED)
		err(1, "mmap");

	close(fd);
}

int main(int argc, char **argv)
{
	size_t raw_len, stripped_len;
	void *raw_addr, *stripped_addr;
	FILE *outfile;
	char *name, *tmp;
	int namelen;

	if (argc != 4) {
		printf("Usage: vdso2c RAW_INPUT STRIPPED_INPUT OUTPUT\n");
		return 1;
	}

	name = strdup(argv[3]);
	namelen = strlen(name);
	if (namelen >= 3 && !strcmp(name + namelen - 3, ".so")) {
		name = NULL;
	} else {
		tmp = strrchr(name, '/');
		if (tmp)
			name = tmp + 1;
		tmp = strchr(name, '.');
		if (tmp)
			*tmp = '\0';
		for (tmp = name; *tmp; tmp++)
			if (*tmp == '-')
				*tmp = '_';
	}

	map_input(argv[1], &raw_addr, &raw_len, PROT_READ);
	map_input(argv[2], &stripped_addr, &stripped_len, PROT_READ);

	outfilename = argv[3];
	outfile = fopen(outfilename, "w");
	if (!outfile)
		err(1, "fopen(%s)", outfilename);

	go(raw_addr, raw_len, stripped_addr, stripped_len, outfile, name);

	munmap(raw_addr, raw_len);
	munmap(stripped_addr, stripped_len);
	fclose(outfile);

	return 0;
}
