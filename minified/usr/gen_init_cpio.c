#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <fcntl.h>
#include <errno.h>
#include <limits.h>

#define xstr(s) #s
#define str(s) xstr(s)
#define MIN(a, b) ((a) < (b) ? (a) : (b))

static unsigned int offset;
static unsigned int ino = 721;
static time_t default_mtime;

static void push_string(const char *name)
{
	unsigned int name_len = strlen(name) + 1;

	fputs(name, stdout);
	putchar(0);
	offset += name_len;
}

static void push_pad(void)
{
	while (offset & 3) {
		putchar(0);
		offset++;
	}
}

static void push_rest(const char *name)
{
	unsigned int name_len = strlen(name) + 1;
	unsigned int tmp_ofs;

	fputs(name, stdout);
	putchar(0);
	offset += name_len;

	tmp_ofs = name_len + 110;
	while (tmp_ofs & 3) {
		putchar(0);
		offset++;
		tmp_ofs++;
	}
}

static void push_hdr(const char *s)
{
	fputs(s, stdout);
	offset += 110;
}

static void cpio_trailer(void)
{
	char s[256];
	const char name[] = "TRAILER!!!";

	sprintf(s,
		"070701%08X%08X%08lX%08lX%08X%08lX"
		"%08X%08X%08X%08X%08X%08X%08X",
		0, 0, (long)0, (long)0, 1, (long)0, 0, 0, 0, 0, 0,
		(unsigned)strlen(name) + 1, 0);
	push_hdr(s);
	push_rest(name);

	while (offset % 512) {
		putchar(0);
		offset++;
	}
}

static int cpio_mkdir_line(const char *line)
{
	char name[PATH_MAX + 1];
	char s[256];
	unsigned int mode;
	int uid;
	int gid;
	const char *n;

	if (4 != sscanf(line, "%" str(PATH_MAX) "s %o %d %d", name, &mode, &uid,
			&gid)) {
		fprintf(stderr, "Unrecognized dir format '%s'", line);
		return -1;
	}
	n = name[0] == '/' ? name + 1 : name;
	mode |= S_IFDIR;
	sprintf(s,
		"070701%08X%08X%08lX%08lX%08X%08lX"
		"%08X%08X%08X%08X%08X%08X%08X",
		ino++, mode, (long)uid, (long)gid, 2, (long)default_mtime, 0, 3,
		1, 0, 0, (unsigned)strlen(n) + 1, 0);
	push_hdr(s);
	push_rest(n);
	return 0;
}

static int cpio_mkfile(const char *name, const char *location,
		       unsigned int mode, uid_t uid, gid_t gid)
{
	char s[256];
	struct stat buf;
	int file = -1;
	int retval;
	int rc = -1;
	int namesize;

	mode |= S_IFREG;

	file = open(location, O_RDONLY);
	if (file < 0) {
		fprintf(stderr, "File %s could not be opened for reading\n",
			location);
		goto error;
	}

	retval = fstat(file, &buf);
	if (retval) {
		fprintf(stderr, "File %s could not be stat()'ed\n", location);
		goto error;
	}

	if (name[0] == '/')
		name++;
	namesize = strlen(name) + 1;
	sprintf(s,
		"070701%08X%08X%08lX%08lX%08X%08lX"
		"%08lX%08X%08X%08X%08X%08X%08X",
		ino, mode, (long)uid, (long)gid, 1, (long)buf.st_mtime,
		(long)buf.st_size, 3, 1, 0, 0, namesize, 0);
	push_hdr(s);
	push_string(name);
	push_pad();

	{
		unsigned long size = buf.st_size;
		while (size) {
			unsigned char filebuf[65536];
			ssize_t this_read;
			size_t this_size = MIN(size, sizeof(filebuf));

			this_read = read(file, filebuf, this_size);
			if (this_read <= 0 || this_read > this_size) {
				fprintf(stderr, "Can not read %s file\n",
					location);
				goto error;
			}

			if (fwrite(filebuf, this_read, 1, stdout) != 1) {
				fprintf(stderr, "writing filebuf failed\n");
				goto error;
			}
			offset += this_read;
			size -= this_read;
		}
	}
	push_pad();

	ino++;
	rc = 0;

error:
	if (file >= 0)
		close(file);
	return rc;
}

static int cpio_mkfile_line(const char *line)
{
	char name[PATH_MAX + 1];
	char location[PATH_MAX + 1];
	unsigned int mode;
	int uid;
	int gid;

	if (5 > sscanf(line, "%" str(PATH_MAX) "s %" str(PATH_MAX) "s %o %d %d",
		       name, location, &mode, &uid, &gid)) {
		fprintf(stderr, "Unrecognized file format '%s'", line);
		return -1;
	}
	return cpio_mkfile(name, location, mode, uid, gid);
}

#define LINE_SIZE (2 * PATH_MAX + 50)

int main(int argc, char *argv[])
{
	FILE *cpio_list;
	char line[LINE_SIZE];
	char *args, *type;
	int ec = 0;
	int line_nr = 0;
	const char *filename;

	default_mtime = time(NULL);

	if (argc != 2) {
		fprintf(stderr, "Usage: %s <cpio_list>\n", argv[0]);
		exit(1);
	}
	filename = argv[1];
	if (!strcmp(filename, "-"))
		cpio_list = stdin;
	else if (!(cpio_list = fopen(filename, "r"))) {
		fprintf(stderr, "ERROR: unable to open '%s': %s\n", filename,
			strerror(errno));
		exit(1);
	}

	while (fgets(line, LINE_SIZE, cpio_list)) {
		size_t slen = strlen(line);
		int rc;

		line_nr++;

		if ('#' == *line || '\n' == *line)
			continue;

		if (!(type = strtok(line, " \t"))) {
			fprintf(stderr,
				"ERROR: incorrect format, could not locate file type line %d: '%s'\n",
				line_nr, line);
			ec = -1;
			break;
		}

		if ('\n' == *type)
			continue;

		if (slen == strlen(type))
			continue;

		if (!(args = strtok(NULL, "\n"))) {
			fprintf(stderr,
				"ERROR: incorrect format, newline required line %d: '%s'\n",
				line_nr, line);
			ec = -1;
		}

		if (!strcmp(type, "file"))
			rc = cpio_mkfile_line(args);
		else if (!strcmp(type, "dir"))
			rc = cpio_mkdir_line(args);
		else {
			fprintf(stderr, "unknown file type line %d: '%s'\n",
				line_nr, type);
			rc = 0;
		}
		if (rc) {
			ec = rc;
			fprintf(stderr, " line %d\n", line_nr);
		}
	}
	if (ec == 0)
		cpio_trailer();

	exit(ec);
}
