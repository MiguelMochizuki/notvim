/**
 * @file test_editor.h
 * @brief Declaration of the editor test suite.
 */
#ifndef TEST_EDITOR_H
#define TEST_EDITOR_H

/** @brief Free the editor the tests share; called from tearDown() so a failed assertion cannot leak it. */
void test_editor_teardown(void);

/** @brief Run every editor.c test; called from main() in test_main.c. */
void test_editor_suite(void);

#endif
