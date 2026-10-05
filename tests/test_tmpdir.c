/**
 * @file test_tmpdir.c
 * @brief Tests for the tmpdir test helper. setUp() has already created the directory.
 */
#define _DEFAULT_SOURCE /* mkdir under -std=c11 */
#include <errno.h>
#include <stdio.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include "unity.h"
#include "test_tmpdir.h"
#include "tmpdir.h"

/** @brief Read the whole file at @p path into @p buf (NUL-terminated); return its size or -1. */
static long read_file(const char *path, char *buf, size_t size) {
	FILE *f = fopen(path, "r");
	if (!f) return -1;
	size_t n = fread(buf, 1, size - 1, f);
	fclose(f);
	buf[n] = '\0';
	return (long)n;
}

/** @brief The directory made by setUp() exists and is a directory. */
static void test_tmpdir_exists_after_setup(void) {
	struct stat st;
	TEST_ASSERT_EQUAL_INT(0, stat(tmpdir_path("."), &st));
	TEST_ASSERT_TRUE(S_ISDIR(st.st_mode));
}

/** @brief tmpdir_write() stores exactly the given content. */
static void test_tmpdir_write_stores_exact_content(void) {
	const char *p = tmpdir_write("a.txt", "hi\n");
	TEST_ASSERT_NOT_NULL(p);
	char buf[16];
	TEST_ASSERT_EQUAL_INT(3, read_file(p, buf, sizeof(buf)));
	TEST_ASSERT_EQUAL_STRING("hi\n", buf);
}

/** @brief Writing "" creates an empty file. */
static void test_tmpdir_write_empty_content_creates_empty_file(void) {
	const char *p = tmpdir_write("empty.txt", "");
	TEST_ASSERT_NOT_NULL(p);
	struct stat st;
	TEST_ASSERT_EQUAL_INT(0, stat(p, &st));
	TEST_ASSERT_EQUAL_INT(0, st.st_size);
}

/** @brief A second write to the same name replaces the first content. */
static void test_tmpdir_write_overwrites(void) {
	tmpdir_write("a.txt", "first and longer");
	const char *p = tmpdir_write("a.txt", "second");
	char buf[32];
	TEST_ASSERT_EQUAL_INT(6, read_file(p, buf, sizeof(buf)));
	TEST_ASSERT_EQUAL_STRING("second", buf);
}

/** @brief tmpdir_path() only builds a path; it does not create anything. */
static void test_tmpdir_path_does_not_create(void) {
	struct stat st;
	TEST_ASSERT_EQUAL_INT(-1, stat(tmpdir_path("nope"), &st));
	TEST_ASSERT_EQUAL_INT(ENOENT, errno);
}

/** @brief tmpdir_destroy() removes the directory with files and subdirectories inside. */
static void test_tmpdir_destroy_removes_everything(void) {
	char root[128], sub[160], file[192];
	snprintf(root, sizeof(root), "%s", tmpdir_path("."));
	root[strlen(root) - 2] = '\0'; /* drop the trailing "/." */
	snprintf(sub, sizeof(sub), "%s", tmpdir_path("sub"));
	TEST_ASSERT_EQUAL_INT(0, mkdir(sub, 0700));
	snprintf(file, sizeof(file), "%s/f.txt", sub);
	FILE *f = fopen(file, "w");
	TEST_ASSERT_NOT_NULL(f);
	fclose(f);
	tmpdir_write("top.txt", "x");

	tmpdir_destroy();

	struct stat st;
	TEST_ASSERT_EQUAL_INT(-1, stat(root, &st));
	TEST_ASSERT_EQUAL_INT(ENOENT, errno);
}

/** @brief A second tmpdir_destroy() does nothing. */
static void test_tmpdir_destroy_twice_is_safe(void) {
	tmpdir_destroy();
	tmpdir_destroy();
	TEST_PASS();
}

/** @brief With no active directory, path and write return NULL. */
static void test_tmpdir_after_destroy_returns_null(void) {
	tmpdir_destroy();
	TEST_ASSERT_NULL(tmpdir_path("x"));
	TEST_ASSERT_NULL(tmpdir_write("x", ""));
}

/**
 * @brief Fork a child that makes its own directory and then ends.
 *
 * With @p use_sigterm the child raises SIGTERM and calls tmpdir_destroy();
 * otherwise it calls exit(0). The child's directory path goes in @p path.
 *
 * @return The child's wait status.
 */
static int run_child(int use_sigterm, char *path, size_t size) {
	int fds[2];
	TEST_ASSERT_EQUAL_INT(0, pipe(fds));
	pid_t pid = fork();
	TEST_ASSERT_TRUE(pid >= 0);
	if (pid == 0) {
		close(fds[0]);
		tmpdir_destroy(); /* the inherited directory belongs to the parent test */
		tmpdir_create();
		tmpdir_write("x.txt", "x");
		const char *dir = tmpdir_path(".");
		if (write(fds[1], dir, strlen(dir) + 1) < 0) _exit(1);
		close(fds[1]);
		if (!use_sigterm) exit(0);
		raise(SIGTERM);
		tmpdir_destroy(); /* sees the signal and exits with 128 + SIGTERM */
		_exit(99);
	}
	close(fds[1]);
	ssize_t n = read(fds[0], path, size - 1);
	close(fds[0]);
	TEST_ASSERT_TRUE(n > 0);
	path[n] = '\0';
	int status = 0;
	TEST_ASSERT_EQUAL_INT(pid, waitpid(pid, &status, 0));
	return status;
}

/** @brief A process that calls exit() leaves no directory behind. */
static void test_tmpdir_removed_on_exit(void) {
	char path[128];
	int status = run_child(0, path, sizeof(path));
	TEST_ASSERT_TRUE(WIFEXITED(status));
	TEST_ASSERT_EQUAL_INT(0, WEXITSTATUS(status));
	struct stat st;
	TEST_ASSERT_EQUAL_INT(-1, stat(path, &st));
	TEST_ASSERT_EQUAL_INT(ENOENT, errno);
}

/** @brief After SIGTERM the directory is removed and the exit status is 128 + SIGTERM. */
static void test_tmpdir_removed_after_sigterm(void) {
	char path[128];
	int status = run_child(1, path, sizeof(path));
	TEST_ASSERT_TRUE(WIFEXITED(status));
	TEST_ASSERT_EQUAL_INT(128 + SIGTERM, WEXITSTATUS(status));
	struct stat st;
	TEST_ASSERT_EQUAL_INT(-1, stat(path, &st));
	TEST_ASSERT_EQUAL_INT(ENOENT, errno);
}

/** @brief Register every test in this file with Unity. */
void test_tmpdir_suite(void) {
	RUN_TEST(test_tmpdir_exists_after_setup);
	RUN_TEST(test_tmpdir_write_stores_exact_content);
	RUN_TEST(test_tmpdir_write_empty_content_creates_empty_file);
	RUN_TEST(test_tmpdir_write_overwrites);
	RUN_TEST(test_tmpdir_path_does_not_create);
	RUN_TEST(test_tmpdir_destroy_removes_everything);
	RUN_TEST(test_tmpdir_destroy_twice_is_safe);
	RUN_TEST(test_tmpdir_after_destroy_returns_null);
	RUN_TEST(test_tmpdir_removed_on_exit);
	RUN_TEST(test_tmpdir_removed_after_sigterm);
}
