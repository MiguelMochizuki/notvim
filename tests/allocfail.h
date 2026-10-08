/**
 * @file allocfail.h
 * @brief Make an allocation fail on purpose, to test the out-of-memory paths.
 *
 * test_runner is linked with -Wl,--wrap=malloc,--wrap=realloc,--wrap=strdup, so the calls
 * made by the objects of src/ and tests/ go through the wrappers in allocfail.c (the calls
 * made inside libc, such as the ones in getline() or fopen(), do not). Nothing fails
 * until allocfail_after() arms it.
 */
#ifndef ALLOCFAIL_H
#define ALLOCFAIL_H

/**
 * @brief Make one malloc, realloc or strdup call fail with ENOMEM, after @p n successful ones.
 *
 * @param n How many calls still succeed first; 0 makes the very next call fail. Only that
 *          one call fails: allocfail_reset() is called automatically when it does.
 */
void allocfail_after(int n);

/** @brief Disarm: every allocation succeeds again. Safe to call when nothing is armed. */
void allocfail_reset(void);

#endif
