
#define ElfW(type) _ElfW(ELF_BITS, type)
#define _ElfW(bits, type) __ElfW(bits, type)
#define __ElfW(bits, type) Elf##bits##_##type

#define Elf_Addr ElfW(Addr)
#define Elf_Ehdr ElfW(Ehdr)
#define Elf_Phdr ElfW(Phdr)
#define Elf_Shdr ElfW(Shdr)
#define Elf_Sym ElfW(Sym)

static Elf_Ehdr ehdr;
static unsigned long shnum;
static unsigned int shstrndx;
static unsigned int shsymtabndx;
static unsigned int shxsymtabndx;

static int sym_index(Elf_Sym *sym);

struct relocs {
	uint32_t *offset;
	unsigned long count;
	unsigned long size;
};

static struct relocs relocs16;
static struct relocs relocs32;
#define FMT PRIu32

struct section {
	Elf_Shdr shdr;
	struct section *link;
	Elf_Sym *symtab;
	Elf32_Word *xsymtab;
	Elf_Rel *reltab;
	char *strtab;
};
static struct section *secs;

static const char *const sym_regex_kernel[S_NSYMTYPES] = {
	[S_ABS] = "^(VDSO|__crc_)",

	[S_REL] = "^(__init_(begin|end)|"
		  "__x86_cpu_dev_(start|end)|"
		  "(__parainstructions|__alt_instructions)(_end)?|"
		  "__(start|stop)_notes|"
		  "__end_rodata|"
		  "__end_rodata_aligned|"
		  "__initramfs_start|"
		  "(jiffies|jiffies_64)|"
		  "__vvar_page|"
		  "_end)$"
};

static const char *const sym_regex_realmode[S_NSYMTYPES] = {
	[S_REL] = "^pa_",

	[S_SEG] = "^real_mode_seg$",

	[S_LIN] = "^pa_",
};

static const char *const *sym_regex;

static regex_t sym_regex_c[S_NSYMTYPES];
static int is_reloc(enum symtype type, const char *sym_name)
{
	return sym_regex[type] &&
	       !regexec(&sym_regex_c[type], sym_name, 0, NULL, 0);
}

static void regex_init(int use_real_mode)
{
	char errbuf[128];
	int err;
	int i;

	if (use_real_mode)
		sym_regex = sym_regex_realmode;
	else
		sym_regex = sym_regex_kernel;

	for (i = 0; i < S_NSYMTYPES; i++) {
		if (!sym_regex[i])
			continue;

		err = regcomp(&sym_regex_c[i], sym_regex[i],
			      REG_EXTENDED | REG_NOSUB);

		if (err) {
			regerror(err, &sym_regex_c[i], errbuf, sizeof(errbuf));
			die("%s", errbuf);
		}
	}
}

static const char *rel_type(unsigned type)
{
	static char buf[32];
	snprintf(buf, sizeof(buf), "reloc type %u", type);
	return buf;
}

static const char *sec_name(unsigned shndx)
{
	const char *sec_strtab;
	const char *name;
	sec_strtab = secs[shstrndx].strtab;
	name = "<noname>";
	if (shndx < shnum) {
		name = sec_strtab + secs[shndx].shdr.sh_name;
	} else if (shndx == SHN_ABS) {
		name = "ABSOLUTE";
	} else if (shndx == SHN_COMMON) {
		name = "COMMON";
	}
	return name;
}

static const char *sym_name(const char *sym_strtab, Elf_Sym *sym)
{
	const char *name;
	name = "<noname>";
	if (sym->st_name) {
		name = sym_strtab + sym->st_name;
	} else {
		name = sec_name(sym_index(sym));
	}
	return name;
}

/* sym_lookup removed - only used by 64-bit percpu_init */

#if BYTE_ORDER == LITTLE_ENDIAN
#define le16_to_cpu(val) (val)
#define le32_to_cpu(val) (val)
#define le64_to_cpu(val) (val)
#endif
#if BYTE_ORDER == BIG_ENDIAN
#define le16_to_cpu(val) bswap_16(val)
#define le32_to_cpu(val) bswap_32(val)
#define le64_to_cpu(val) bswap_64(val)
#endif

static uint16_t elf16_to_cpu(uint16_t val)
{
	return le16_to_cpu(val);
}

static uint32_t elf32_to_cpu(uint32_t val)
{
	return le32_to_cpu(val);
}

#define elf_half_to_cpu(x) elf16_to_cpu(x)
#define elf_word_to_cpu(x) elf32_to_cpu(x)

#if ELF_BITS == 64
static uint64_t elf64_to_cpu(uint64_t val)
{
	return le64_to_cpu(val);
}
#define elf_addr_to_cpu(x) elf64_to_cpu(x)
#define elf_off_to_cpu(x) elf64_to_cpu(x)
#define elf_xword_to_cpu(x) elf64_to_cpu(x)
#else
#define elf_addr_to_cpu(x) elf32_to_cpu(x)
#define elf_off_to_cpu(x) elf32_to_cpu(x)
#define elf_xword_to_cpu(x) elf32_to_cpu(x)
#endif

static int sym_index(Elf_Sym *sym)
{
	Elf_Sym *symtab = secs[shsymtabndx].symtab;
	Elf32_Word *xsymtab = secs[shxsymtabndx].xsymtab;
	unsigned long offset;
	int index;

	if (sym->st_shndx != SHN_XINDEX)
		return sym->st_shndx;

	offset = (unsigned long)sym - (unsigned long)symtab;
	index = offset / sizeof(*sym);

	return elf32_to_cpu(xsymtab[index]);
}

static void read_ehdr(FILE *fp)
{
	if (fread(&ehdr, sizeof(ehdr), 1, fp) != 1) {
		die("Cannot read ELF header: %s\n", strerror(errno));
	}
	if (memcmp(ehdr.e_ident, ELFMAG, SELFMAG) != 0) {
		die("No ELF magic\n");
	}
	if (ehdr.e_ident[EI_CLASS] != ELF_CLASS) {
		die("Not a %d bit executable\n", ELF_BITS);
	}
	if (ehdr.e_ident[EI_DATA] != ELFDATA2LSB) {
		die("Not a LSB ELF executable\n");
	}
	if (ehdr.e_ident[EI_VERSION] != EV_CURRENT) {
		die("Unknown ELF version\n");
	}

	ehdr.e_type = elf_half_to_cpu(ehdr.e_type);
	ehdr.e_machine = elf_half_to_cpu(ehdr.e_machine);
	ehdr.e_version = elf_word_to_cpu(ehdr.e_version);
	ehdr.e_entry = elf_addr_to_cpu(ehdr.e_entry);
	ehdr.e_phoff = elf_off_to_cpu(ehdr.e_phoff);
	ehdr.e_shoff = elf_off_to_cpu(ehdr.e_shoff);
	ehdr.e_flags = elf_word_to_cpu(ehdr.e_flags);
	ehdr.e_ehsize = elf_half_to_cpu(ehdr.e_ehsize);
	ehdr.e_phentsize = elf_half_to_cpu(ehdr.e_phentsize);
	ehdr.e_phnum = elf_half_to_cpu(ehdr.e_phnum);
	ehdr.e_shentsize = elf_half_to_cpu(ehdr.e_shentsize);
	ehdr.e_shnum = elf_half_to_cpu(ehdr.e_shnum);
	ehdr.e_shstrndx = elf_half_to_cpu(ehdr.e_shstrndx);

	shnum = ehdr.e_shnum;
	shstrndx = ehdr.e_shstrndx;

	if ((ehdr.e_type != ET_EXEC) && (ehdr.e_type != ET_DYN))
		die("Unsupported ELF header type\n");
	if (ehdr.e_machine != ELF_MACHINE)
		die("Not for %s\n", ELF_MACHINE_NAME);
	if (ehdr.e_version != EV_CURRENT)
		die("Unknown ELF version\n");
	if (ehdr.e_ehsize != sizeof(Elf_Ehdr))
		die("Bad Elf header size\n");
	if (ehdr.e_phentsize != sizeof(Elf_Phdr))
		die("Bad program header entry\n");
	if (ehdr.e_shentsize != sizeof(Elf_Shdr))
		die("Bad section header entry\n");

	if (shnum == SHN_UNDEF || shstrndx == SHN_XINDEX) {
		Elf_Shdr shdr;

		if (fseek(fp, ehdr.e_shoff, SEEK_SET) < 0)
			die("Seek to %" FMT " failed: %s\n", ehdr.e_shoff,
			    strerror(errno));

		if (fread(&shdr, sizeof(shdr), 1, fp) != 1)
			die("Cannot read initial ELF section header: %s\n",
			    strerror(errno));

		if (shnum == SHN_UNDEF)
			shnum = elf_xword_to_cpu(shdr.sh_size);

		if (shstrndx == SHN_XINDEX)
			shstrndx = elf_word_to_cpu(shdr.sh_link);
	}

	if (shstrndx >= shnum)
		die("String table index out of bounds\n");
}

static void read_shdrs(FILE *fp)
{
	int i;
	Elf_Shdr shdr;

	secs = calloc(shnum, sizeof(struct section));
	if (!secs) {
		die("Unable to allocate %ld section headers\n", shnum);
	}
	if (fseek(fp, ehdr.e_shoff, SEEK_SET) < 0) {
		die("Seek to %" FMT " failed: %s\n", ehdr.e_shoff,
		    strerror(errno));
	}
	for (i = 0; i < shnum; i++) {
		struct section *sec = &secs[i];
		if (fread(&shdr, sizeof(shdr), 1, fp) != 1)
			die("Cannot read ELF section headers %d/%ld: %s\n", i,
			    shnum, strerror(errno));
		sec->shdr.sh_name = elf_word_to_cpu(shdr.sh_name);
		sec->shdr.sh_type = elf_word_to_cpu(shdr.sh_type);
		sec->shdr.sh_flags = elf_xword_to_cpu(shdr.sh_flags);
		sec->shdr.sh_addr = elf_addr_to_cpu(shdr.sh_addr);
		sec->shdr.sh_offset = elf_off_to_cpu(shdr.sh_offset);
		sec->shdr.sh_size = elf_xword_to_cpu(shdr.sh_size);
		sec->shdr.sh_link = elf_word_to_cpu(shdr.sh_link);
		sec->shdr.sh_info = elf_word_to_cpu(shdr.sh_info);
		sec->shdr.sh_addralign = elf_xword_to_cpu(shdr.sh_addralign);
		sec->shdr.sh_entsize = elf_xword_to_cpu(shdr.sh_entsize);
		if (sec->shdr.sh_link < shnum)
			sec->link = &secs[sec->shdr.sh_link];
	}
}

static void read_strtabs(FILE *fp)
{
	int i;
	for (i = 0; i < shnum; i++) {
		struct section *sec = &secs[i];
		if (sec->shdr.sh_type != SHT_STRTAB) {
			continue;
		}
		sec->strtab = malloc(sec->shdr.sh_size);
		if (!sec->strtab) {
			die("malloc of %" FMT " bytes for strtab failed\n",
			    sec->shdr.sh_size);
		}
		if (fseek(fp, sec->shdr.sh_offset, SEEK_SET) < 0) {
			die("Seek to %" FMT " failed: %s\n",
			    sec->shdr.sh_offset, strerror(errno));
		}
		if (fread(sec->strtab, 1, sec->shdr.sh_size, fp) !=
		    sec->shdr.sh_size) {
			die("Cannot read symbol table: %s\n", strerror(errno));
		}
	}
}

static void read_symtabs(FILE *fp)
{
	int i, j;

	for (i = 0; i < shnum; i++) {
		struct section *sec = &secs[i];
		int num_syms;

		switch (sec->shdr.sh_type) {
		case SHT_SYMTAB_SHNDX:
			sec->xsymtab = malloc(sec->shdr.sh_size);
			if (!sec->xsymtab) {
				die("malloc of %" FMT
				    " bytes for xsymtab failed\n",
				    sec->shdr.sh_size);
			}
			if (fseek(fp, sec->shdr.sh_offset, SEEK_SET) < 0) {
				die("Seek to %" FMT " failed: %s\n",
				    sec->shdr.sh_offset, strerror(errno));
			}
			if (fread(sec->xsymtab, 1, sec->shdr.sh_size, fp) !=
			    sec->shdr.sh_size) {
				die("Cannot read extended symbol table: %s\n",
				    strerror(errno));
			}
			shxsymtabndx = i;
			continue;

		case SHT_SYMTAB:
			num_syms = sec->shdr.sh_size / sizeof(Elf_Sym);

			sec->symtab = malloc(sec->shdr.sh_size);
			if (!sec->symtab) {
				die("malloc of %" FMT
				    " bytes for symtab failed\n",
				    sec->shdr.sh_size);
			}
			if (fseek(fp, sec->shdr.sh_offset, SEEK_SET) < 0) {
				die("Seek to %" FMT " failed: %s\n",
				    sec->shdr.sh_offset, strerror(errno));
			}
			if (fread(sec->symtab, 1, sec->shdr.sh_size, fp) !=
			    sec->shdr.sh_size) {
				die("Cannot read symbol table: %s\n",
				    strerror(errno));
			}
			for (j = 0; j < num_syms; j++) {
				Elf_Sym *sym = &sec->symtab[j];

				sym->st_name = elf_word_to_cpu(sym->st_name);
				sym->st_value = elf_addr_to_cpu(sym->st_value);
				sym->st_size = elf_xword_to_cpu(sym->st_size);
				sym->st_shndx = elf_half_to_cpu(sym->st_shndx);
			}
			shsymtabndx = i;
			continue;

		default:
			continue;
		}
	}
}

static void read_relocs(FILE *fp)
{
	int i, j;
	for (i = 0; i < shnum; i++) {
		struct section *sec = &secs[i];
		if (sec->shdr.sh_type != SHT_REL_TYPE) {
			continue;
		}
		sec->reltab = malloc(sec->shdr.sh_size);
		if (!sec->reltab) {
			die("malloc of %" FMT " bytes for relocs failed\n",
			    sec->shdr.sh_size);
		}
		if (fseek(fp, sec->shdr.sh_offset, SEEK_SET) < 0) {
			die("Seek to %" FMT " failed: %s\n",
			    sec->shdr.sh_offset, strerror(errno));
		}
		if (fread(sec->reltab, 1, sec->shdr.sh_size, fp) !=
		    sec->shdr.sh_size) {
			die("Cannot read symbol table: %s\n", strerror(errno));
		}
		for (j = 0; j < sec->shdr.sh_size / sizeof(Elf_Rel); j++) {
			Elf_Rel *rel = &sec->reltab[j];
			rel->r_offset = elf_addr_to_cpu(rel->r_offset);
			rel->r_info = elf_xword_to_cpu(rel->r_info);
#if (SHT_REL_TYPE == SHT_RELA)
			rel->r_addend = elf_xword_to_cpu(rel->r_addend);
#endif
		}
	}
}

/* print_absolute_relocs removed - --abs-relocs never used in this config */

static void add_reloc(struct relocs *r, uint32_t offset)
{
	if (r->count == r->size) {
		unsigned long newsize = r->size + 50000;
		void *mem = realloc(r->offset, newsize * sizeof(r->offset[0]));

		if (!mem)
			die("realloc of %ld entries for relocs failed\n",
			    newsize);
		r->offset = mem;
		r->size = newsize;
	}
	r->offset[r->count++] = offset;
}

static void walk_relocs(int (*process)(struct section *sec, Elf_Rel *rel,
				       Elf_Sym *sym, const char *symname))
{
	int i;

	for (i = 0; i < shnum; i++) {
		char *sym_strtab;
		Elf_Sym *sh_symtab;
		struct section *sec_applies, *sec_symtab;
		int j;
		struct section *sec = &secs[i];

		if (sec->shdr.sh_type != SHT_REL_TYPE) {
			continue;
		}
		sec_symtab = sec->link;
		sec_applies = &secs[sec->shdr.sh_info];
		if (!(sec_applies->shdr.sh_flags & SHF_ALLOC)) {
			continue;
		}
		sh_symtab = sec_symtab->symtab;
		sym_strtab = sec_symtab->link->strtab;
		for (j = 0; j < sec->shdr.sh_size / sizeof(Elf_Rel); j++) {
			Elf_Rel *rel = &sec->reltab[j];
			Elf_Sym *sym = &sh_symtab[ELF_R_SYM(rel->r_info)];
			const char *symname = sym_name(sym_strtab, sym);

			process(sec, rel, sym, symname);
		}
	}
}

/* percpu_init, is_percpu_sym, do_reloc64 removed - 64-bit support removed */

static int do_reloc32(struct section *sec, Elf_Rel *rel, Elf_Sym *sym,
		      const char *symname)
{
	unsigned r_type = ELF32_R_TYPE(rel->r_info);
	int shn_abs = (sym->st_shndx == SHN_ABS) && !is_reloc(S_REL, symname);

	switch (r_type) {
	case R_386_NONE:
	case R_386_PC32:
	case R_386_PC16:
	case R_386_PC8:
	case R_386_PLT32:

		break;

	case R_386_32:
		if (shn_abs) {
			if (is_reloc(S_ABS, symname))
				break;

			die("Invalid absolute %s relocation: %s\n",
			    rel_type(r_type), symname);
			break;
		}

		add_reloc(&relocs32, rel->r_offset);
		break;

	default:
		die("Unsupported relocation type: %s (%d)\n", rel_type(r_type),
		    r_type);
		break;
	}

	return 0;
}

static int do_reloc_real(struct section *sec, Elf_Rel *rel, Elf_Sym *sym,
			 const char *symname)
{
	unsigned r_type = ELF32_R_TYPE(rel->r_info);
	int shn_abs = (sym->st_shndx == SHN_ABS) && !is_reloc(S_REL, symname);

	switch (r_type) {
	case R_386_NONE:
	case R_386_PC32:
	case R_386_PC16:
	case R_386_PC8:
	case R_386_PLT32:

		break;

	case R_386_16:
		if (shn_abs) {
			if (is_reloc(S_ABS, symname))
				break;

			if (is_reloc(S_SEG, symname)) {
				add_reloc(&relocs16, rel->r_offset);
				break;
			}
		} else {
			if (!is_reloc(S_LIN, symname))
				break;
		}
		die("Invalid %s %s relocation: %s\n",
		    shn_abs ? "absolute" : "relative", rel_type(r_type),
		    symname);
		break;

	case R_386_32:
		if (shn_abs) {
			if (is_reloc(S_ABS, symname))
				break;

			if (is_reloc(S_REL, symname)) {
				add_reloc(&relocs32, rel->r_offset);
				break;
			}
		} else {
			if (is_reloc(S_LIN, symname))
				add_reloc(&relocs32, rel->r_offset);
			break;
		}
		die("Invalid %s %s relocation: %s\n",
		    shn_abs ? "absolute" : "relative", rel_type(r_type),
		    symname);
		break;

	default:
		die("Unsupported relocation type: %s (%d)\n", rel_type(r_type),
		    r_type);
		break;
	}

	return 0;
}

static int cmp_relocs(const void *va, const void *vb)
{
	const uint32_t *a, *b;
	a = va;
	b = vb;
	return (*a == *b) ? 0 : (*a > *b) ? 1 : -1;
}

static void sort_relocs(struct relocs *r)
{
	qsort(r->offset, r->count, sizeof(r->offset[0]), cmp_relocs);
}

static int write32(uint32_t v, FILE *f)
{
	unsigned char buf[4];

	put_unaligned_le32(v, buf);
	return fwrite(buf, 1, 4, f) == 4 ? 0 : -1;
}

/* write32_as_text removed - --text never used */

static void emit_relocs(int use_real_mode)
{
	int i;
	int (*do_reloc)(struct section *sec, Elf_Rel *rel, Elf_Sym *sym,
			const char *symname);

	if (!use_real_mode)
		do_reloc = do_reloc32;
	else
		do_reloc = do_reloc_real;

	walk_relocs(do_reloc);

	if (relocs16.count && !use_real_mode)
		die("Segment relocations found but --realmode not specified\n");

	sort_relocs(&relocs32);
	sort_relocs(&relocs16);

	if (use_real_mode) {
		write32(relocs16.count, stdout);
		for (i = 0; i < relocs16.count; i++)
			write32(relocs16.offset[i], stdout);

		write32(relocs32.count, stdout);
		for (i = 0; i < relocs32.count; i++)
			write32(relocs32.offset[i], stdout);
	} else {
		write32(0, stdout);

		for (i = 0; i < relocs32.count; i++)
			write32(relocs32.offset[i], stdout);
	}
}

#define process process_32

void process(FILE *fp, int use_real_mode)
{
	regex_init(use_real_mode);
	read_ehdr(fp);
	read_shdrs(fp);
	read_strtabs(fp);
	read_symtabs(fp);
	read_relocs(fp);
	emit_relocs(use_real_mode);
}
