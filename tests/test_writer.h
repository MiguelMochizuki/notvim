/**
 * @file test_writer.h
 * @brief Declaration of the writer and ':w' command test suite.
 */
#ifndef TEST_WRITER_H
#define TEST_WRITER_H

/** @brief Free the editor shared by the tests of test_writer.c; called from tearDown(). Safe on an untouched editor. */
void test_writer_teardown(void);

/** @brief Run every writer.c and commands.c test; called from main() in test_main.c. */
void test_writer_suite(void);

#endif
