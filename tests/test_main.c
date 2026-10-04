/**
 * @file test_main.c
 * @brief Unity test runner: calls one suite function per module.
 */
#include "unity.h"
#include "test_editor.h"

/** @brief Run all suites. @return Number of failed tests (0 on success). */
int main(void) {
	UNITY_BEGIN();
	test_editor_suite();
	return UNITY_END();
}
