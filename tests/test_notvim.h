/**
 * @file test_notvim.h
 * @brief Declaration of the notvim binary test suite.
 */
#ifndef TEST_NOTVIM_H
#define TEST_NOTVIM_H

/** @brief Forget the pty size of the last spawn; called from tearDown() so screen() cannot inherit it from an earlier test. */
void test_notvim_teardown(void);

/** @brief Run the end-to-end tests; called from main() in test_main.c. */
void test_notvim_suite(void);

#endif
