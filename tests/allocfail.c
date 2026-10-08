/**
 * @file allocfail.c
 * @brief Wrappers for malloc, realloc and strdup that fail once on demand; see allocfail.h.
 */
#define _POSIX_C_SOURCE 200809L /* strdup under -std=c11 */
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include "allocfail.h"

void *__real_malloc(size_t size);
void *__real_realloc(void *ptr, size_t size);
char *__real_strdup(const char *s);

/** Calls that still succeed before the failing one; negative when nothing is armed. */
static int countdown = -1;

void allocfail_after(int n) { countdown = n; }
void allocfail_reset(void) { countdown = -1; }

/** @brief Count one call. @return Non-zero if this call must fail (and disarm, with errno set to ENOMEM). */
static int should_fail(void) {
	if (countdown < 0) return 0;
	if (countdown > 0) {
		countdown--;
		return 0;
	}
	countdown = -1;
	errno = ENOMEM;
	return 1;
}

void *__wrap_malloc(size_t size) { return should_fail() ? NULL : __real_malloc(size); }
void *__wrap_realloc(void *ptr, size_t size) { return should_fail() ? NULL : __real_realloc(ptr, size); }
char *__wrap_strdup(const char *s) { return should_fail() ? NULL : __real_strdup(s); }
