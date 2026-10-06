/**
 * @file test_motions.h
 * @brief Declaration of the motions test suite.
 */
#ifndef TEST_MOTIONS_H
#define TEST_MOTIONS_H

/** @brief Free the shared editor of the motions tests; called from tearDown(). Safe on an untouched editor. */
void test_motions_teardown(void);

/** @brief Run every test of motions.c and of the motion keys of the editor; called from main() in test_main.c. */
void test_motions_suite(void);

#endif
