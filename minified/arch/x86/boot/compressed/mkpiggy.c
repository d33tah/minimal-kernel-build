
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>

static inline uint32_t get_unaligned_le32(const void *p)
{
	const uint8_t *b = (const uint8_t *)p;
	return b[0] | b[1] << 8 | b[2] << 16 | b[3] << 24;
}

int main(int argc, char *argv[])
{
	uint32_t olen;
	long ilen;
	FILE *f = NULL;
	int retval = 1;

	if (argc < 2) {
		fprintf(stderr, "Usage: %s compressed_file\n", argv[0]);
		goto bail;
	}

	f = fopen(argv[1], "r");
	if (!f) {
		perror(argv[1]);
		goto bail;
	}

	if (fseek(f, -4L, SEEK_END)) {
		perror(argv[1]);
	}

	if (fread(&olen, sizeof(olen), 1, f) != 1) {
		perror(argv[1]);
		goto bail;
	}

	ilen = ftell(f);
	olen = get_unaligned_le32(&olen);

	printf(".section \".rodata..compressed\",\"a\",@progbits\n");
	printf(".globl z_input_len\n");
	printf("z_input_len = %lu\n", ilen);
	printf(".globl z_output_len\n");
	printf("z_output_len = %lu\n", (unsigned long)olen);

	printf(".globl input_data, input_data_end\n");
	printf("input_data:\n");
	printf(".incbin \"%s\"\n", argv[1]);
	printf("input_data_end:\n");

	printf(".section \".rodata\",\"a\",@progbits\n");
	printf(".globl input_len\n");
	printf("input_len:\n\t.long %lu\n", ilen);
	printf(".globl output_len\n");
	printf("output_len:\n\t.long %lu\n", (unsigned long)olen);

	retval = 0;
bail:
	if (f)
		fclose(f);
	return retval;
}
