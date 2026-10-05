/**
 * @file test_main.c
 * @brief Unity test runner: calls one suite function per module.
 */
#include "unity.h"
#include "test_editor.h"
#include "test_keys.h"
#include "test_cmdline.h"
#include "test_terminal.h"
#include "test_notvim.h"
#include "test_stopsig.h"
#include "test_winch.h"
#include "test_utf8.h"
#include "test_tmpdir.h"
#include "tmpdir.h"

/** @brief Unity hook run before each test; creates the temporary directory. */
void setUp(void) { tmpdir_create(); }
/** @brief Unity hook run after each test, even a failed one; frees the shared editor and removes the temporary directory. */
void tearDown(void) {
	test_editor_teardown();
	tmpdir_destroy();
}

/** @brief Run all suites. @return Number of failed tests (0 on success). */
int main(void) {
	UNITY_BEGIN();
	test_editor_suite();
	test_keys_suite();
	test_cmdline_suite();
	test_stopsig_suite();
	test_winch_suite();
	test_utf8_suite();
	test_terminal_suite();
	test_notvim_suite();
	test_tmpdir_suite();
	return UNITY_END();
}
