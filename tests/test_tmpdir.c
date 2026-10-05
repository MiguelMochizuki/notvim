/**
 * @file test_tmpdir.c
 * @brief Tests for the tmpdir test helper. setUp() has already created the directory.
 */
#define _DEFAULT_SOURCE /* mkdir under -std=c11 */
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
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
}
