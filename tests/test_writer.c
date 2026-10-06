/**
 * @file test_writer.c
 * @brief Unit tests for writer.c (editor_write_file) and the ':w' command of commands.c.
 */
#define _POSIX_C_SOURCE 200809L /* symlink, lstat under -std=c11 */
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "unity.h"
#include "test_writer.h"
#include "editor.h"
#include "keys.h"
#include "writer.h"
#include "tmpdir.h"

/** Editor shared by the tests of this file; freed in test_writer_teardown(), even after a failed assertion. */
static editor_t e;

void test_writer_teardown(void) {
	editor_free(&e);
}

/** @brief Read the whole file @p path into @p buf (NUL-terminated); return its length, or -1 if it can't be opened. */
static long slurp(const char *path, char *buf, size_t size) {
	FILE *f = fopen(path, "rb");
	if (!f) return -1;
	size_t n = fread(buf, 1, size - 1, f);
	fclose(f);
	buf[n] = '\0';
	return (long)n;
}

/** @brief Assert that the file @p path holds exactly the @p len bytes of @p want. */
static void assert_file(const char *path, const char *want, size_t len) {
	char buf[4096];
	TEST_ASSERT_EQUAL_INT((long)len, slurp(path, buf, sizeof(buf)));
	if (len) TEST_ASSERT_EQUAL_MEMORY(want, buf, len);
}

/** @brief Number of directory entries in the temporary directory, "." and ".." not counted. */
static int entries(void) {
	DIR *d = opendir(tmpdir_path("."));
	int n = 0;
	if (!d) return -1;
	for (struct dirent *de; (de = readdir(d));) if (strcmp(de->d_name, ".") && strcmp(de->d_name, "..")) n++;
	closedir(d);
	return n;
}

/** @brief Fill the shared editor with the given lines. */
static void lines(const char *a, const char *b) {
	editor_init(&e);
	if (a) editor_append_line(&e, a);
	if (b) editor_append_line(&e, b);
}

/** @brief Two lines are written with LF, the counts are returned, and no temporary file stays. */
static void test_writer_writes_lines_with_lf(void) {
	char path[256];
	snprintf(path, sizeof(path), "%s", tmpdir_path("o.txt"));
	lines("ab", "cd");
	size_t l = 0, b = 0;
	TEST_ASSERT_EQUAL_INT(0, editor_write_file(&e, path, &l, &b));
	assert_file(path, "ab\ncd\n", 6);
	TEST_ASSERT_EQUAL_UINT(2, l);
	TEST_ASSERT_EQUAL_UINT(6, b);
	TEST_ASSERT_EQUAL_INT(1, entries());
}

/** @brief A new file from an editor that is not CRLF never gets a CR. */
static void test_writer_new_file_has_no_cr(void) {
	char path[256];
	snprintf(path, sizeof(path), "%s", tmpdir_path("n.txt"));
	lines("a", "b");
	e.crlf = 0;
	TEST_ASSERT_EQUAL_INT(0, editor_write_file(&e, path, NULL, NULL));
	assert_file(path, "a\nb\n", 4);
}

/** @brief The crlf flag writes "\r\n" after each line. */
static void test_writer_crlf_flag_writes_crlf(void) {
	char path[256];
	snprintf(path, sizeof(path), "%s", tmpdir_path("d.txt"));
	lines("a", "b");
	e.crlf = 1;
	size_t b = 0;
	TEST_ASSERT_EQUAL_INT(0, editor_write_file(&e, path, NULL, &b));
	assert_file(path, "a\r\nb\r\n", 6);
	TEST_ASSERT_EQUAL_UINT(6, b);
}

/** @brief A lone '\r' inside a line is data: written unchanged with and without the crlf flag. */
static void test_writer_lone_cr_is_data(void) {
	char path[256];
	snprintf(path, sizeof(path), "%s", tmpdir_path("cr.txt"));
	lines("a\rb", "c\r");
	TEST_ASSERT_EQUAL_INT(0, editor_write_file(&e, path, NULL, NULL));
	assert_file(path, "a\rb\nc\r\n", 7);
	e.crlf = 1;
	TEST_ASSERT_EQUAL_INT(0, editor_write_file(&e, path, NULL, NULL));
	assert_file(path, "a\rb\r\nc\r\r\n", 9);
}

/** @brief Invalid UTF-8 and control bytes are written as stored. */
static void test_writer_bytes_as_stored(void) {
	char path[256];
	snprintf(path, sizeof(path), "%s", tmpdir_path("bin.txt"));
	lines("a\xff\xfe\x80z", "\x1b[0m\x7f");
	TEST_ASSERT_EQUAL_INT(0, editor_write_file(&e, path, NULL, NULL));
	assert_file(path, "a\xff\xfe\x80z\n\x1b[0m\x7f\n", 12);
}

/** @brief No lines is an empty file; one empty line is "\n". */
static void test_writer_empty_editor_and_empty_line(void) {
	char path[256];
	snprintf(path, sizeof(path), "%s", tmpdir_path("em.txt"));
	lines(NULL, NULL);
	size_t l = 9, b = 9;
	TEST_ASSERT_EQUAL_INT(0, editor_write_file(&e, path, &l, &b));
	assert_file(path, "", 0);
	TEST_ASSERT_EQUAL_UINT(0, l);
	TEST_ASSERT_EQUAL_UINT(0, b);
	lines("", NULL);
	TEST_ASSERT_EQUAL_INT(0, editor_write_file(&e, path, &l, &b));
	assert_file(path, "\n", 1);
	TEST_ASSERT_EQUAL_UINT(1, l);
	TEST_ASSERT_EQUAL_UINT(1, b);
}

/** @brief An existing file is replaced whole, also by a shorter text. */
static void test_writer_replaces_an_existing_file(void) {
	char path[256];
	snprintf(path, sizeof(path), "%s", tmpdir_write("old.txt", "a long old content\nmore\n"));
	lines("x", NULL);
	TEST_ASSERT_EQUAL_INT(0, editor_write_file(&e, path, NULL, NULL));
	assert_file(path, "x\n", 2);
	TEST_ASSERT_EQUAL_INT(1, entries());
}

/** @brief A missing directory is an error (ENOENT) and nothing is created. */
static void test_writer_missing_directory(void) {
	char path[300];
	snprintf(path, sizeof(path), "%s/nodir/f.txt", tmpdir_path("."));
	lines("x", NULL);
	TEST_ASSERT_EQUAL_INT(-1, editor_write_file(&e, path, NULL, NULL));
	TEST_ASSERT_EQUAL_INT(ENOENT, errno);
	TEST_ASSERT_EQUAL_INT(0, entries());
}

/** @brief An unwritable directory is an error, the original file is intact and no temporary file stays. */
static void test_writer_unwritable_directory_keeps_the_original(void) {
	if (geteuid() == 0) TEST_IGNORE_MESSAGE("root ignores directory permissions");
	char path[256];
	snprintf(path, sizeof(path), "%s", tmpdir_write("keep.txt", "original\n"));
	TEST_ASSERT_EQUAL_INT(0, chmod(tmpdir_path("."), 0555));
	lines("new", NULL);
	int rc = editor_write_file(&e, path, NULL, NULL);
	int err = errno;
	chmod(tmpdir_path("."), 0755);
	TEST_ASSERT_EQUAL_INT(-1, rc);
	TEST_ASSERT_EQUAL_INT(EACCES, err);
	assert_file(path, "original\n", 9);
	TEST_ASSERT_EQUAL_INT(1, entries());
}

/** @brief A directory as target is an error, it stays a directory and no temporary file stays. */
static void test_writer_target_is_a_directory(void) {
	char path[256];
	snprintf(path, sizeof(path), "%s", tmpdir_path("adir"));
	TEST_ASSERT_EQUAL_INT(0, mkdir(path, 0755));
	lines("x", NULL);
	TEST_ASSERT_EQUAL_INT(-1, editor_write_file(&e, path, NULL, NULL));
	struct stat st;
	TEST_ASSERT_EQUAL_INT(0, stat(path, &st));
	TEST_ASSERT_TRUE(S_ISDIR(st.st_mode));
	TEST_ASSERT_EQUAL_INT(1, entries());
}

/** @brief The write is atomic: a descriptor opened on the old file still sees the old bytes after the write, and the path has the new ones. */
static void test_writer_replaces_the_file_atomically(void) {
	char path[256], buf[64];
	snprintf(path, sizeof(path), "%s", tmpdir_write("at.txt", "old\n"));
	int fd = open(path, O_RDONLY);
	TEST_ASSERT_TRUE(fd >= 0);
	lines("new", NULL);
	int rc = editor_write_file(&e, path, NULL, NULL);
	ssize_t n = read(fd, buf, sizeof(buf));
	close(fd);
	TEST_ASSERT_EQUAL_INT(0, rc);
	TEST_ASSERT_EQUAL_INT(4, n);
	TEST_ASSERT_EQUAL_MEMORY("old\n", buf, 4);
	assert_file(path, "new\n", 4);
}

/** @brief The permission bits of an existing file are kept. */
static void test_writer_keeps_the_permission_bits(void) {
	char path[256];
	snprintf(path, sizeof(path), "%s", tmpdir_write("perm.txt", "x\n"));
	lines("y", NULL);
	mode_t modes[] = { 0640, 0755, 0600 };
	for (size_t i = 0; i < 3; i++) {
		TEST_ASSERT_EQUAL_INT(0, chmod(path, modes[i]));
		TEST_ASSERT_EQUAL_INT(0, editor_write_file(&e, path, NULL, NULL));
		struct stat st;
		TEST_ASSERT_EQUAL_INT(0, stat(path, &st));
		TEST_ASSERT_EQUAL_UINT((unsigned)modes[i], (unsigned)(st.st_mode & 07777));
	}
}

/** @brief A new file gets 0666 minus the umask. */
static void test_writer_new_file_follows_the_umask(void) {
	char path[256];
	snprintf(path, sizeof(path), "%s", tmpdir_path("um.txt"));
	mode_t old = umask(022);
	lines("y", NULL);
	int rc = editor_write_file(&e, path, NULL, NULL);
	umask(old);
	TEST_ASSERT_EQUAL_INT(0, rc);
	struct stat st;
	TEST_ASSERT_EQUAL_INT(0, stat(path, &st));
	TEST_ASSERT_EQUAL_UINT(0644, (unsigned)(st.st_mode & 07777));
}

/** @brief Writing to a symbolic link replaces the file it points to and keeps the link. */
static void test_writer_writes_through_a_symlink(void) {
	char target[256], link[256];
	snprintf(target, sizeof(target), "%s", tmpdir_write("real.txt", "old\n"));
	snprintf(link, sizeof(link), "%s", tmpdir_path("link.txt"));
	TEST_ASSERT_EQUAL_INT(0, symlink("real.txt", link));
	lines("new", NULL);
	TEST_ASSERT_EQUAL_INT(0, editor_write_file(&e, link, NULL, NULL));
	struct stat st;
	TEST_ASSERT_EQUAL_INT(0, lstat(link, &st));
	TEST_ASSERT_TRUE(S_ISLNK(st.st_mode));
	assert_file(target, "new\n", 4);
	TEST_ASSERT_EQUAL_INT(2, entries());
}

/** @brief Load then write of an unmodified file gives the same bytes: LF, CRLF, UTF-8, invalid bytes, a lone CR, mixed, empty. */
static void test_writer_round_trips(void) {
	static const struct { const char *data; size_t len; } cases[] = {
		{ "a\nb\n", 4 }, { "a\r\nb\r\n", 6 }, { "h\xc3\xa9llo\n\xe2\x82\xac\n", 11 }, { "\xff\xfe\x80\n\xc3\n", 6 },
		{ "a\rb\n", 4 }, { "a\r\nb\n", 5 }, { "", 0 }, { "\n", 1 }, { "\n\n\n", 3 }, { "tab\there\n", 9 }, { "\x1b[2J\n", 5 },
	};
	char src[256], out[256];
	snprintf(out, sizeof(out), "%s", tmpdir_path("out.txt"));
	for (size_t i = 0; i < sizeof(cases) / sizeof(*cases); i++) {
		snprintf(src, sizeof(src), "%s", tmpdir_write_bytes("src.txt", cases[i].data, cases[i].len));
		editor_init(&e);
		TEST_ASSERT_EQUAL_INT(0, editor_load_file(&e, src));
		TEST_ASSERT_EQUAL_INT(0, editor_write_file(&e, out, NULL, NULL));
		assert_file(out, cases[i].data, cases[i].len);
		editor_free(&e);
	}
}

/** @brief A file without a final newline gains one, as Vim does (Vim 9.1 checked: "a\nb" is written as "a\nb\n"). */
static void test_writer_adds_the_missing_final_newline(void) {
	char src[256], out[256];
	snprintf(src, sizeof(src), "%s", tmpdir_write("nof.txt", "a\nb"));
	snprintf(out, sizeof(out), "%s", tmpdir_path("out.txt"));
	editor_init(&e);
	TEST_ASSERT_EQUAL_INT(0, editor_load_file(&e, src));
	TEST_ASSERT_EQUAL_INT(0, editor_write_file(&e, out, NULL, NULL));
	assert_file(out, "a\nb\n", 4);
}

/** @brief Type the bytes of @p keys into the shared editor; 0x1b is the Esc key. */
static void type(const char *keys) {
	for (; *keys; keys++) editor_handle_key(&e, *keys == 0x1b ? KEY_ESC : (unsigned char)*keys);
}

/** @brief Assert that the message of the shared editor is @p want. */
static void assert_msg(const char *want) {
	TEST_ASSERT_NOT_NULL(e.msg);
	TEST_ASSERT_EQUAL_STRING(want, e.msg);
}

/** @brief ":w" without a path and without a name is E32 and writes nothing. */
static void test_command_w_without_a_name(void) {
	editor_init(&e);
	editor_append_line(&e, "x");
	e.modified = 1;
	type(":w\r");
	assert_msg("E32: No file name");
	TEST_ASSERT_NULL(e.path);
	TEST_ASSERT_EQUAL_INT(1, e.modified);
	TEST_ASSERT_EQUAL_INT(0, entries());
}

/** @brief ":w name" on an editor with no path writes the file, adopts the name as the path, clears modified and shows [New]. */
static void test_command_w_name_adopts_the_path(void) {
	char path[256], cmd[300], want[400];
	snprintf(path, sizeof(path), "%s", tmpdir_path("new.txt"));
	editor_init(&e);
	type("ihi\r\x1b"); /* lines "hi" and "" */
	TEST_ASSERT_EQUAL_INT(1, e.modified);
	snprintf(cmd, sizeof(cmd), ":w %s\r", path);
	type(cmd);
	assert_file(path, "hi\n\n", 4);
	TEST_ASSERT_NOT_NULL(e.path);
	TEST_ASSERT_EQUAL_STRING(path, e.path);
	TEST_ASSERT_EQUAL_INT(0, e.modified);
	snprintf(want, sizeof(want), "\"%s\" [New] 2L, 4B written", path);
	assert_msg(want);
}

/** @brief ":w" on a loaded file writes it back, clears modified, and shows the counts without [New]; ":w!" does the same. */
static void test_command_w_and_w_bang_save_the_file(void) {
	char path[256], want[400];
	snprintf(path, sizeof(path), "%s", tmpdir_write("f.txt", "ab\ncd\n"));
	const char *cmds[] = { ":w\r", ":w!\r" };
	for (int i = 0; i < 2; i++) {
		editor_free(&e);
		tmpdir_write("f.txt", "ab\ncd\n");
		editor_init(&e);
		TEST_ASSERT_EQUAL_INT(0, editor_load_file(&e, path));
		type("ix\x1b");
		TEST_ASSERT_EQUAL_INT(1, e.modified);
		type(cmds[i]);
		assert_file(path, "xab\ncd\n", 7);
		TEST_ASSERT_EQUAL_INT(0, e.modified);
		snprintf(want, sizeof(want), "\"%s\" 2L, 7B written", path);
		assert_msg(want);
		TEST_ASSERT_EQUAL_INT(EDITOR_MODE_NORMAL, e.mode);
	}
}

/** @brief A file that does not exist yet, loaded by name, is created by ":w" and shows [New]. */
static void test_command_w_creates_the_loaded_missing_file(void) {
	char path[256], want[400];
	snprintf(path, sizeof(path), "%s", tmpdir_path("missing.txt"));
	editor_init(&e);
	TEST_ASSERT_EQUAL_INT(0, editor_load_file(&e, path));
	type("iq\x1b:w\r");
	assert_file(path, "q\n", 2);
	snprintf(want, sizeof(want), "\"%s\" [New] 1L, 2B written", path);
	assert_msg(want);
}

/** @brief A CRLF file is written back with CRLF and the message says [dos]; an LF file never gets a CR. */
static void test_command_w_keeps_the_line_endings(void) {
	char path[256], want[400];
	snprintf(path, sizeof(path), "%s", tmpdir_write("dos.txt", "ab\r\ncd\r\n"));
	editor_init(&e);
	TEST_ASSERT_EQUAL_INT(0, editor_load_file(&e, path));
	type("ix\x1b:w\r");
	assert_file(path, "xab\r\ncd\r\n", 9);
	snprintf(want, sizeof(want), "\"%s\" [dos] 2L, 9B written", path);
	assert_msg(want);
	editor_free(&e);
	snprintf(path, sizeof(path), "%s", tmpdir_write("unix.txt", "ab\ncd\n"));
	editor_init(&e);
	TEST_ASSERT_EQUAL_INT(0, editor_load_file(&e, path));
	type("ix\x1b:w\r");
	assert_file(path, "xab\ncd\n", 7);
}

/** @brief ":w other" on an editor that has a path writes the other file, keeps the path and leaves modified set, as Vim does. */
static void test_command_w_other_name_keeps_the_path(void) {
	char path[256], other[256], cmd[300];
	snprintf(path, sizeof(path), "%s", tmpdir_write("a.txt", "a\n"));
	snprintf(other, sizeof(other), "%s", tmpdir_path("b.txt"));
	editor_init(&e);
	TEST_ASSERT_EQUAL_INT(0, editor_load_file(&e, path));
	type("ix\x1b");
	snprintf(cmd, sizeof(cmd), ":w %s\r", other);
	type(cmd);
	assert_file(other, "xa\n", 3);
	assert_file(path, "a\n", 2);
	TEST_ASSERT_EQUAL_STRING(path, e.path);
	TEST_ASSERT_EQUAL_INT(1, e.modified);
}

/** @brief ":w name" on an existing file that is not the buffer's own shows E13 and leaves the file, modified and path alone. */
static void test_command_w_refuses_an_existing_other_file(void) {
	char path[256], other[256], cmd[300];
	snprintf(path, sizeof(path), "%s", tmpdir_write("a.txt", "a\n"));
	snprintf(other, sizeof(other), "%s", tmpdir_write("b.txt", "old\n"));
	editor_init(&e);
	TEST_ASSERT_EQUAL_INT(0, editor_load_file(&e, path));
	type("ix\x1b");
	snprintf(cmd, sizeof(cmd), ":w %s\r", other);
	type(cmd);
	assert_msg("E13: File exists (add ! to override)");
	assert_file(other, "old\n", 4);
	TEST_ASSERT_EQUAL_INT(1, e.modified);
	TEST_ASSERT_EQUAL_STRING(path, e.path);
	TEST_ASSERT_EQUAL_INT(EDITOR_MODE_NORMAL, e.mode);
}

/** @brief A buffer with no name refuses an existing file too, and adopts no path. */
static void test_command_w_refuses_an_existing_file_without_a_path(void) {
	char other[256], cmd[300];
	snprintf(other, sizeof(other), "%s", tmpdir_write("b.txt", "old\n"));
	editor_init(&e);
	type("ix\x1b");
	snprintf(cmd, sizeof(cmd), ":w %s\r", other);
	type(cmd);
	assert_msg("E13: File exists (add ! to override)");
	assert_file(other, "old\n", 4);
	TEST_ASSERT_NULL(e.path);
	TEST_ASSERT_EQUAL_INT(1, e.modified);
}

/** @brief ":w! name" overwrites an existing other file (and, as in Vim, a read-only one), keeping modified and the path. */
static void test_command_w_bang_overwrites_an_existing_other_file(void) {
	char path[256], other[256], cmd[300], want[400];
	snprintf(path, sizeof(path), "%s", tmpdir_write("a.txt", "a\n"));
	snprintf(other, sizeof(other), "%s", tmpdir_write("b.txt", "old\n"));
	editor_init(&e);
	TEST_ASSERT_EQUAL_INT(0, editor_load_file(&e, path));
	type("ix\x1b");
	snprintf(cmd, sizeof(cmd), ":w! %s\r", other);
	type(cmd);
	assert_file(other, "xa\n", 3);
	snprintf(want, sizeof(want), "\"%s\" 1L, 3B written", other);
	assert_msg(want);
	TEST_ASSERT_EQUAL_INT(1, e.modified);
	chmod(other, 0444);
	if (geteuid() != 0) { /* the bit check is meaningless as root, the overwrite itself is not */
		type("iy\x1b");
		type(cmd);
		assert_file(other, "yxa\n", 4);
	}
}

/** @brief Writing to the buffer's own file is allowed in any spelling: no name, the same path, a symlink to it, "./name". */
static void test_command_w_own_file_in_any_spelling_is_allowed(void) {
	char path[256], link[256], dotted[300], cmd[400];
	snprintf(path, sizeof(path), "%s", tmpdir_write("a.txt", "a\n"));
	snprintf(link, sizeof(link), "%s", tmpdir_path("ln.txt"));
	TEST_ASSERT_EQUAL_INT(0, symlink(path, link));
	snprintf(dotted, sizeof(dotted), "%s/./a.txt", tmpdir_path("."));
	const char *names[] = { "", path, link, dotted };
	const char *want[] = { "xa\n", "xxa\n", "xxxa\n", "xxxxa\n" };
	editor_init(&e);
	TEST_ASSERT_EQUAL_INT(0, editor_load_file(&e, path));
	for (int i = 0; i < 4; i++) {
		type("ix\x1b");
		snprintf(cmd, sizeof(cmd), ":w %s\r", names[i]);
		type(cmd);
		assert_file(path, want[i], strlen(want[i]));
		TEST_ASSERT_EQUAL_INT(0, e.modified);
	}
}

/** @brief ":wq other" and ":x other" refuse an existing other file and do not quit; with '!' they write and quit. */
static void test_command_wq_and_x_refuse_an_existing_other_file(void) {
	char path[256], other[256], cmd[300];
	snprintf(path, sizeof(path), "%s", tmpdir_write("a.txt", "a\n"));
	snprintf(other, sizeof(other), "%s", tmpdir_write("b.txt", "old\n"));
	const char *fmt[] = { ":wq %s\r", ":x %s\r" };
	for (int i = 0; i < 2; i++) {
		editor_free(&e);
		editor_init(&e);
		TEST_ASSERT_EQUAL_INT(0, editor_load_file(&e, path));
		type("ix\x1b");
		snprintf(cmd, sizeof(cmd), fmt[i], other);
		type(cmd);
		assert_msg("E13: File exists (add ! to override)");
		TEST_ASSERT_EQUAL_INT(0, e.quit);
		assert_file(other, "old\n", 4);
	}
	snprintf(cmd, sizeof(cmd), ":wq! %s\r", other);
	type(cmd);
	TEST_ASSERT_EQUAL_INT(1, e.quit);
	assert_file(other, "xa\n", 3);
}

/** @brief ":w" on the buffer's own read-only file shows E45 and writes nothing; ":w!" writes and keeps the 0444 bits. */
static void test_command_w_refuses_a_read_only_own_file(void) {
	if (geteuid() == 0) TEST_IGNORE_MESSAGE("root can write read-only files");
	char path[256], want[400];
	snprintf(path, sizeof(path), "%s", tmpdir_write("ro.txt", "old\n"));
	TEST_ASSERT_EQUAL_INT(0, chmod(path, 0444));
	editor_init(&e);
	TEST_ASSERT_EQUAL_INT(0, editor_load_file(&e, path));
	type("ix\x1b:w\r");
	assert_msg("E45: 'readonly' option is set (add ! to override)");
	assert_file(path, "old\n", 4);
	TEST_ASSERT_EQUAL_INT(1, e.modified);
	type(":wq\r");
	TEST_ASSERT_EQUAL_INT(0, e.quit);
	type(":w!\r");
	assert_file(path, "xold\n", 5);
	TEST_ASSERT_EQUAL_INT(0, e.modified);
	snprintf(want, sizeof(want), "\"%s\" 1L, 5B written", path);
	assert_msg(want);
	struct stat st;
	TEST_ASSERT_EQUAL_INT(0, stat(path, &st));
	TEST_ASSERT_EQUAL_INT(0444, st.st_mode & 07777);
}

/** @brief An empty editor is saved as an empty file with "0L, 0B written". */
static void test_command_w_empty_editor(void) {
	char path[256], cmd[300], want[400];
	snprintf(path, sizeof(path), "%s", tmpdir_path("empty.txt"));
	editor_init(&e);
	snprintf(cmd, sizeof(cmd), ":w %s\r", path);
	type(cmd);
	assert_file(path, "", 0);
	snprintf(want, sizeof(want), "\"%s\" [New] 0L, 0B written", path);
	assert_msg(want);
}

/** @brief A failed write keeps modified, adopts no path, and shows the strerror() text. */
static void test_command_w_error_shows_strerror(void) {
	char path[300], cmd[400];
	snprintf(path, sizeof(path), "%s/nodir/f.txt", tmpdir_path("."));
	editor_init(&e);
	type("ix\x1b");
	snprintf(cmd, sizeof(cmd), ":w %s\r", path);
	type(cmd);
	TEST_ASSERT_EQUAL_INT(1, e.modified);
	TEST_ASSERT_NULL(e.path);
	TEST_ASSERT_NOT_NULL(e.msg);
	TEST_ASSERT_NOT_NULL(strstr(e.msg, strerror(ENOENT)));
	TEST_ASSERT_NULL(strstr(e.msg, "written"));
}

/** @brief The next key clears the "written" message. */
static void test_command_w_message_goes_with_the_next_key(void) {
	char path[256];
	snprintf(path, sizeof(path), "%s", tmpdir_write("k.txt", "a\n"));
	editor_init(&e);
	TEST_ASSERT_EQUAL_INT(0, editor_load_file(&e, path));
	type(":w\r");
	TEST_ASSERT_NOT_NULL(e.msg);
	editor_handle_key(&e, 'j');
	TEST_ASSERT_NULL(e.msg);
}

/** @brief Run the writer and ':w' tests. */
/** The refusal Vim gives for a quit with unsaved changes. */
#define E37 "E37: No write since last change (add ! to override)"

/** @brief Start the shared editor on a saved file "q.txt" holding "ab\n", with @c modified set if @p modified. */
static void open_q(int modified) {
	editor_free(&e);
	TEST_ASSERT_EQUAL_INT(0, editor_load_file(&e, tmpdir_write("q.txt", "ab\n")));
	if (modified) type("ix\x1b");
	TEST_ASSERT_EQUAL_INT(modified, e.modified);
}

/** @brief ":q" quits an unmodified buffer and is refused with E37 on a modified one (which stays modified and unsaved). */
static void test_command_q_quits_only_when_unmodified(void) {
	open_q(0);
	type(":q\r");
	TEST_ASSERT_EQUAL_INT(1, e.quit);
	open_q(1);
	type(":q\r");
	TEST_ASSERT_EQUAL_INT(0, e.quit);
	assert_msg(E37);
	TEST_ASSERT_EQUAL_INT(1, e.modified);
	assert_file(e.path, "ab\n", 3);
}

/** @brief A modified buffer without a name is refused too. */
static void test_command_q_refuses_an_unnamed_modified_buffer(void) {
	editor_free(&e);
	type("ihi\x1b:q\r");
	TEST_ASSERT_EQUAL_INT(0, e.quit);
	assert_msg(E37);
}

/** @brief ":q!" quits a modified buffer without writing it. */
static void test_command_q_bang_quits_without_saving(void) {
	open_q(1);
	type(":q!\r");
	TEST_ASSERT_EQUAL_INT(1, e.quit);
	assert_file(e.path, "ab\n", 3);
	TEST_ASSERT_EQUAL_INT(1, e.modified);
	open_q(0);
	type(":q!\r");
	TEST_ASSERT_EQUAL_INT(1, e.quit);
}

/** @brief ":wq" (and ":wq!") writes the buffer and quits, modified or not. */
static void test_command_wq_writes_and_quits(void) {
	const char *cmds[] = { ":wq\r", ":wq!\r" };
	for (int i = 0; i < 2; i++) {
		open_q(1);
		type(cmds[i]);
		TEST_ASSERT_EQUAL_INT(1, e.quit);
		assert_file(e.path, "xab\n", 4);
		TEST_ASSERT_EQUAL_INT(0, e.modified);
	}
	open_q(0);
	type(":wq\r");
	TEST_ASSERT_EQUAL_INT(1, e.quit);
}

/** @brief ":wq name" writes to that name and quits, as Vim does. */
static void test_command_wq_with_a_name_writes_there_and_quits(void) {
	char path[256], cmd[300];
	snprintf(path, sizeof(path), "%s", tmpdir_path("other.txt"));
	open_q(1);
	snprintf(cmd, sizeof(cmd), ":wq %s\r", path);
	type(cmd);
	TEST_ASSERT_EQUAL_INT(1, e.quit);
	assert_file(path, "xab\n", 4);
}

/** @brief A failing write in ":wq" and ":x" shows the error and does not quit; so does a missing name (E32). */
static void test_command_wq_and_x_do_not_quit_when_the_write_fails(void) {
	const char *cmds[] = { ":wq\r", ":x\r", ":wq!\r", ":x!\r" };
	for (int i = 0; i < 4; i++) {
		open_q(1);
		free(e.path);
		e.path = strdup(tmpdir_path("nodir/q.txt"));
		type(cmds[i]);
		TEST_ASSERT_EQUAL_INT(0, e.quit);
		TEST_ASSERT_EQUAL_INT(1, e.modified);
		TEST_ASSERT_NOT_NULL(strstr(e.msg, "No such file or directory"));
	}
	editor_free(&e);
	type("ihi\x1b:wq\r");
	TEST_ASSERT_EQUAL_INT(0, e.quit);
	assert_msg("E32: No file name");
	type(":x\r");
	TEST_ASSERT_EQUAL_INT(0, e.quit);
	assert_msg("E32: No file name");
}

/** @brief ":x" writes a modified buffer and quits; an unmodified one is not written (its file is left alone) but quits. */
static void test_command_x_writes_only_when_modified(void) {
	open_q(1);
	type(":x\r");
	TEST_ASSERT_EQUAL_INT(1, e.quit);
	assert_file(e.path, "xab\n", 4);
	open_q(0);
	TEST_ASSERT_EQUAL_INT(0, unlink(e.path)); /* a write would bring it back */
	type(":x\r");
	TEST_ASSERT_EQUAL_INT(1, e.quit);
	TEST_ASSERT_NOT_EQUAL(0, access(e.path, F_OK));
	editor_free(&e); /* unnamed and unmodified: nothing to write, so no E32 */
	type(":x\r");
	TEST_ASSERT_EQUAL_INT(1, e.quit);
}

/** @brief "ZZ" is ":x": it writes a modified buffer and quits; an unmodified one is left alone. */
static void test_normal_zz_is_x(void) {
	open_q(1);
	type("ZZ");
	TEST_ASSERT_EQUAL_INT(1, e.quit);
	assert_file(e.path, "xab\n", 4);
	open_q(0);
	unlink(e.path);
	type("ZZ");
	TEST_ASSERT_EQUAL_INT(1, e.quit);
	TEST_ASSERT_NOT_EQUAL(0, access(e.path, F_OK));
	open_q(1);
	free(e.path);
	e.path = strdup(tmpdir_path("nodir/q.txt"));
	type("ZZ");
	TEST_ASSERT_EQUAL_INT(0, e.quit); /* a failed write does not quit */
	TEST_ASSERT_NOT_NULL(strstr(e.msg, "No such file or directory"));
}

/** @brief "ZQ" is ":q!": it quits a modified buffer without writing it. */
static void test_normal_zq_is_q_bang(void) {
	open_q(1);
	type("ZQ");
	TEST_ASSERT_EQUAL_INT(1, e.quit);
	assert_file(e.path, "ab\n", 3);
}

/** @brief A "Z" waits for the next key: it asks for no redraw, another key cancels it and is handled as usual, Z then Z again is not stuck. */
static void test_normal_z_pending_state(void) {
	open_q(1);
	TEST_ASSERT_EQUAL_INT(0, editor_handle_key(&e, 'Z'));
	TEST_ASSERT_EQUAL_INT(1, e.pending.prefix == 'Z');
	TEST_ASSERT_EQUAL_INT(1, editor_handle_key(&e, 'l')); /* moves, as Vim's "Zl" does */
	TEST_ASSERT_EQUAL_INT(0, e.pending.prefix == 'Z');
	TEST_ASSERT_EQUAL_UINT(1, e.cx);
	TEST_ASSERT_EQUAL_INT(0, e.quit);
	type("Zi"); /* the second key still acts: insert mode */
	TEST_ASSERT_EQUAL_INT(EDITOR_MODE_INSERT, e.mode);
	type("\x1b");
	type("Z\x1b"); /* Esc cancels */
	TEST_ASSERT_EQUAL_INT(0, e.pending.prefix == 'Z');
	type("Zz"); /* an unknown key cancels too: a later "Z" is not completed by it */
	type("Z");
	TEST_ASSERT_EQUAL_INT(0, e.quit);
	TEST_ASSERT_EQUAL_INT(1, e.pending.prefix == 'Z');
	type("q"); /* "Zq" is not a command: lower case */
	TEST_ASSERT_EQUAL_INT(0, e.quit);
	TEST_ASSERT_EQUAL_INT(0, e.pending.prefix == 'Z');
	type("Z:"); /* the colon opens the command line */
	TEST_ASSERT_EQUAL_INT(EDITOR_MODE_COMMAND, e.mode);
	TEST_ASSERT_EQUAL_INT(0, e.quit);
}

/** @brief In insert mode and on the command line Z is typed, and nothing quits. */
static void test_z_is_a_plain_character_in_insert_and_command_mode(void) {
	editor_free(&e);
	type("iZZQ");
	TEST_ASSERT_EQUAL_STRING("ZZQ", editor_line(&e, 0));
	TEST_ASSERT_EQUAL_INT(0, e.pending.prefix == 'Z');
	TEST_ASSERT_EQUAL_INT(0, e.quit);
	type("\x1b:ZQ");
	TEST_ASSERT_EQUAL_STRING("ZQ", e.cmd.text);
	TEST_ASSERT_EQUAL_INT(0, e.quit);
}

/** @brief Ctrl+Q quits an unmodified buffer in every mode and is refused with E37 when modified; the editor goes on. */
static void test_ctrl_q_quits_unless_modified(void) {
	open_q(0);
	editor_handle_key(&e, 0x11);
	TEST_ASSERT_EQUAL_INT(1, e.quit);
	open_q(0);
	type("i");
	editor_handle_key(&e, 0x11);
	TEST_ASSERT_EQUAL_INT(1, e.quit);
	open_q(0);
	type(":");
	editor_handle_key(&e, 0x11);
	TEST_ASSERT_EQUAL_INT(1, e.quit);
	open_q(1);
	TEST_ASSERT_NOT_EQUAL(0, editor_handle_key(&e, 0x11));
	TEST_ASSERT_EQUAL_INT(0, e.quit);
	assert_msg(E37);
	type(":q!\r");
	TEST_ASSERT_EQUAL_INT(1, e.quit);
}

/** @brief Ctrl+Q in insert mode on a buffer just typed in is refused too, and the next key brings back the status line. */
static void test_ctrl_q_refused_in_insert_mode_after_typing(void) {
	editor_free(&e);
	type("ihi");
	editor_handle_key(&e, 0x11);
	TEST_ASSERT_EQUAL_INT(0, e.quit);
	assert_msg(E37);
	TEST_ASSERT_EQUAL_INT(EDITOR_MODE_INSERT, e.mode);
	type("!");
	TEST_ASSERT_NULL(e.msg);
	TEST_ASSERT_EQUAL_STRING("hi!", editor_line(&e, 0));
}

/** @brief Any key that is not the second Z or Q clears a pending Z, Ctrl+Q included: a refused Ctrl+Q must not leave "Z" half typed. */
static void test_ctrl_q_clears_a_pending_z(void) {
	open_q(1);
	type("Z");
	TEST_ASSERT_EQUAL_INT(1, e.pending.prefix == 'Z');
	editor_handle_key(&e, 0x11);
	TEST_ASSERT_EQUAL_INT(0, e.quit);
	TEST_ASSERT_EQUAL_INT(0, e.pending.prefix == 'Z');
	type("Z");
	TEST_ASSERT_EQUAL_INT(0, e.quit); /* a lone Z waits again, it does not run :x */
	TEST_ASSERT_EQUAL_INT(1, e.pending.prefix == 'Z');
}

/** @brief A refused Ctrl+Q ends a half-typed UTF-8 character: its continuation byte afterwards does not complete it. */
static void test_ctrl_q_drops_a_half_typed_character(void) {
	editor_free(&e);
	type("ia\xc3");
	editor_handle_key(&e, 0x11);
	type("\xa9");
	TEST_ASSERT_EQUAL_STRING("a", editor_line(&e, 0));
}

/** @brief Load and free clear a quit request and a pending Z. */
static void test_quit_state_is_reset_by_free_and_load(void) {
	open_q(0);
	type(":q\r");
	editor_free(&e);
	TEST_ASSERT_EQUAL_INT(0, e.quit);
	editor_free(&e);
	type("Z");
	editor_free(&e);
	TEST_ASSERT_EQUAL_INT(0, e.pending.prefix == 'Z');
}

void test_writer_suite(void) {
	RUN_TEST(test_writer_writes_lines_with_lf);
	RUN_TEST(test_writer_new_file_has_no_cr);
	RUN_TEST(test_writer_crlf_flag_writes_crlf);
	RUN_TEST(test_writer_lone_cr_is_data);
	RUN_TEST(test_writer_bytes_as_stored);
	RUN_TEST(test_writer_empty_editor_and_empty_line);
	RUN_TEST(test_writer_replaces_an_existing_file);
	RUN_TEST(test_writer_missing_directory);
	RUN_TEST(test_writer_unwritable_directory_keeps_the_original);
	RUN_TEST(test_writer_target_is_a_directory);
	RUN_TEST(test_writer_replaces_the_file_atomically);
	RUN_TEST(test_writer_keeps_the_permission_bits);
	RUN_TEST(test_writer_new_file_follows_the_umask);
	RUN_TEST(test_writer_writes_through_a_symlink);
	RUN_TEST(test_writer_round_trips);
	RUN_TEST(test_writer_adds_the_missing_final_newline);
	RUN_TEST(test_command_w_without_a_name);
	RUN_TEST(test_command_w_name_adopts_the_path);
	RUN_TEST(test_command_w_and_w_bang_save_the_file);
	RUN_TEST(test_command_w_creates_the_loaded_missing_file);
	RUN_TEST(test_command_w_keeps_the_line_endings);
	RUN_TEST(test_command_w_other_name_keeps_the_path);
	RUN_TEST(test_command_w_refuses_an_existing_other_file);
	RUN_TEST(test_command_w_refuses_an_existing_file_without_a_path);
	RUN_TEST(test_command_w_bang_overwrites_an_existing_other_file);
	RUN_TEST(test_command_w_own_file_in_any_spelling_is_allowed);
	RUN_TEST(test_command_wq_and_x_refuse_an_existing_other_file);
	RUN_TEST(test_command_w_refuses_a_read_only_own_file);
	RUN_TEST(test_command_w_empty_editor);
	RUN_TEST(test_command_w_error_shows_strerror);
	RUN_TEST(test_command_w_message_goes_with_the_next_key);
	RUN_TEST(test_command_q_quits_only_when_unmodified);
	RUN_TEST(test_command_q_refuses_an_unnamed_modified_buffer);
	RUN_TEST(test_command_q_bang_quits_without_saving);
	RUN_TEST(test_command_wq_writes_and_quits);
	RUN_TEST(test_command_wq_with_a_name_writes_there_and_quits);
	RUN_TEST(test_command_wq_and_x_do_not_quit_when_the_write_fails);
	RUN_TEST(test_command_x_writes_only_when_modified);
	RUN_TEST(test_normal_zz_is_x);
	RUN_TEST(test_normal_zq_is_q_bang);
	RUN_TEST(test_normal_z_pending_state);
	RUN_TEST(test_z_is_a_plain_character_in_insert_and_command_mode);
	RUN_TEST(test_ctrl_q_quits_unless_modified);
	RUN_TEST(test_ctrl_q_refused_in_insert_mode_after_typing);
	RUN_TEST(test_ctrl_q_clears_a_pending_z);
	RUN_TEST(test_ctrl_q_drops_a_half_typed_character);
	RUN_TEST(test_quit_state_is_reset_by_free_and_load);
}
