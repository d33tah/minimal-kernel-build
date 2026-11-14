/*
 * The WASI calls of site/wasi.js through wasi-libc, the way a program
 * built with wasi-sdk makes them (clang.wasm itself, and the kbuild host
 * tools of plan step E0d): files, directories, stdin, clocks and the
 * errors a program checks for. test_clang_worker.py runs it in the page's
 * ClangWorker in each browser with "probe\n" on stdin and the working
 * directory empty. One line per check, "ok NAME" or "FAIL NAME: errno N";
 * the last line is "PROBE OK" (exit code 3, to see the code pass through
 * proc_exit) or "PROBE FAILED" (exit code 1).
 *
 * Built in CI with wasi-sdk 29 (the pin of gen-clang-wasm.sh):
 *   clang --target=wasm32-wasip1 -O2 -o wasi-probe.wasm wasi-probe.c
 */
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static int bad;

static void check(const char *name, int ok)
{
	if (ok)
		printf("ok %s\n", name);
	else {
		printf("FAIL %s: errno %d\n", name, errno);
		bad++;
	}
}

int main(void)
{
	char buf[4096];
	struct stat st;
	struct timespec ts;
	ssize_t n;
	int fd, i, seen;

	n = read(0, buf, sizeof(buf) - 1);
	buf[n > 0 ? n : 0] = 0;
	check("stdin", n == 6 && !strcmp(buf, "probe\n"));
	check("stdin_eof", read(0, buf, 10) == 0);
	check("clock_realtime", clock_gettime(CLOCK_REALTIME, &ts) == 0 && ts.tv_sec > 1700000000);
	check("clock_monotonic", clock_gettime(CLOCK_MONOTONIC, &ts) == 0);

	check("mkdir", mkdir("d", 0777) == 0);
	check("mkdir_exists", mkdir("d", 0777) == -1 && errno == EEXIST);
	check("mkdir_no_parent", mkdir("none/x", 0777) == -1 && errno == ENOENT);

	fd = open("d/a.txt", O_CREAT | O_WRONLY | O_TRUNC, 0666);
	check("create", fd >= 0);
	check("write", write(fd, "hello ", 6) == 6);
	close(fd);
	fd = open("d/a.txt", O_WRONLY | O_APPEND);
	check("append", fd >= 0 && write(fd, "world", 5) == 5);
	close(fd);
	fd = open("d/a.txt", O_WRONLY);
	check("set_append", fcntl(fd, F_SETFL, O_APPEND) == 0 && write(fd, "!", 1) == 1);
	close(fd);

	fd = open("d/a.txt", O_RDONLY);
	n = read(fd, buf, sizeof(buf) - 1);
	buf[n > 0 ? n : 0] = 0;
	check("read", n == 12 && !strcmp(buf, "hello world!"));
	check("read_eof", read(fd, buf, 10) == 0);
	check("seek_end", lseek(fd, -6, SEEK_END) == 6);
	check("read_after_seek", read(fd, buf, 100) == 6 && !memcmp(buf, "world!", 6));
	check("seek_cur", lseek(fd, -1, SEEK_CUR) == 11);
	check("seek_negative", lseek(fd, -100, SEEK_SET) == -1 && errno == EINVAL);
	check("pread", pread(fd, buf, 5, 0) == 5 && !memcmp(buf, "hello", 5));
	check("pread_past_end", pread(fd, buf, 5, 100) == 0);
	close(fd);

	check("stat_file", stat("d/a.txt", &st) == 0 && S_ISREG(st.st_mode) && st.st_size == 12);
	check("stat_dir", stat("d", &st) == 0 && S_ISDIR(st.st_mode));
	check("stat_missing", stat("d/none", &st) == -1 && errno == ENOENT);
	check("stat_dotdot", stat("d/../d/./a.txt", &st) == 0 && st.st_size == 12);
	fd = open("d/a.txt", O_RDWR);
	check("truncate_grow", ftruncate(fd, 20) == 0 && fstat(fd, &st) == 0 && st.st_size == 20);
	check("truncate_zeros", pread(fd, buf, 8, 12) == 8 && !memcmp(buf, "\0\0\0\0\0\0\0\0", 8));
	pwrite(fd, "abcdefgh", 8, 12);
	check("truncate_shrink", ftruncate(fd, 5) == 0 && fstat(fd, &st) == 0 && st.st_size == 5);
	check("gap_after_shrink", pwrite(fd, "Z", 1, 10) == 1 && pread(fd, buf, 5, 5) == 5 && !memcmp(buf, "\0\0\0\0\0", 5));
	check("set_times", futimens(fd, NULL) == 0);
	check("pwrite", pwrite(fd, "HE", 2, 0) == 2 && pread(fd, buf, 5, 0) == 5 && !memcmp(buf, "HEllo", 5));
	check("fsync", fsync(fd) == 0);
	close(fd);

	check("create_excl", open("d/a.txt", O_CREAT | O_EXCL | O_WRONLY, 0666) == -1 && errno == EEXIST);
	check("open_missing", open("d/none", O_RDONLY) == -1 && errno == ENOENT);
	check("open_no_parent", open("none/x", O_CREAT | O_WRONLY, 0666) == -1 && errno == ENOENT);
	check("open_dir_for_write", open("d", O_WRONLY) == -1 && errno == EISDIR);
	check("open_file_as_dir", open("d/a.txt", O_RDONLY | O_DIRECTORY) == -1 && errno == ENOTDIR);
	check("read_dir", (fd = open("d", O_RDONLY)) >= 0 && read(fd, buf, 10) == -1 && errno == EISDIR);
	close(fd);

	/* A file that grows by many small writes, then far past its end. */
	fd = open("big", O_CREAT | O_RDWR, 0666);
	memset(buf, 'x', sizeof(buf));
	for (i = 0; i < 256 && write(fd, buf, sizeof(buf)) == sizeof(buf); i++)
		;
	check("grow", i == 256 && fstat(fd, &st) == 0 && st.st_size == 256 * 4096);
	check("write_past_end", pwrite(fd, "y", 1, 2000000) == 1 && fstat(fd, &st) == 0 && st.st_size == 2000001);
	check("hole_reads_zero", pread(fd, buf, 4, 1500000) == 4 && !memcmp(buf, "\0\0\0\0", 4));
	close(fd);

	seen = 0;
	DIR *dir = opendir("d");
	struct dirent *e;
	while (dir && (e = readdir(dir)))
		seen += !strcmp(e->d_name, "a.txt") ? 1 : !strcmp(e->d_name, ".") || !strcmp(e->d_name, "..") ? 10 : 100;
	check("readdir", dir && seen == 21);
	if (dir)
		closedir(dir);

	check("rename", rename("d/a.txt", "d/b.txt") == 0 && stat("d/a.txt", &st) == -1 && stat("d/b.txt", &st) == 0);
	check("rename_missing", rename("d/a.txt", "d/c.txt") == -1 && errno == ENOENT);
	check("rmdir_not_empty", rmdir("d") == -1 && errno == ENOTEMPTY);
	check("rmdir_file", rmdir("d/b.txt") == -1 && errno == ENOTDIR);
	check("unlink_dir", unlink("d") == -1 && errno == EISDIR);
	check("unlink", unlink("d/b.txt") == 0 && stat("d/b.txt", &st) == -1);
	check("unlink_missing", unlink("d/b.txt") == -1 && errno == ENOENT);
	check("rmdir", rmdir("d") == 0 && stat("d", &st) == -1 && errno == ENOENT);
	check("rmdir_missing", rmdir("d") == -1 && errno == ENOENT);
	check("symlink_unsupported", symlink("big", "s") == -1 && errno == ENOTSUP);
	check("link_unsupported", link("big", "l") == -1 && errno == ENOTSUP);
	check("readlink_not_link", readlink("big", buf, 10) == -1 && errno == EINVAL);
	check("sleep_unsupported", nanosleep(&(struct timespec){ 0, 1000 }, NULL) == -1 && errno == ENOTSUP);

	fprintf(stderr, "probe stderr\n");
	printf("%s\n", bad ? "PROBE FAILED" : "PROBE OK");
	return bad ? 1 : 3;
}
