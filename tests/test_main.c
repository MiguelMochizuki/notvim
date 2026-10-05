/**
 * @file test_main.c
 * @brief Unity test runner: calls one suite function per module.
 */
#include "unity.h"
#include "test_editor.h"
#include "test_terminal.h"
#include "test_notvim.h"

/** @brief Unity hook run before each test; nothing to prepare. */
void setUp(void) {}
/** @brief Unity hook run after each test; nothing to clean up. */
void tearDown(void) {}

/** @brief Run all suites. @return Number of failed tests (0 on success). */
int main(void) {
	UNITY_BEGIN();
	test_editor_suite();
	test_terminal_suite();
	test_notvim_suite();
	return UNITY_END();
}
