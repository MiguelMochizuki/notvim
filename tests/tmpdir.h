/**
 * @file tmpdir.h
 * @brief Per-test temporary directory for tests that read or write files.
 *
 * Call tmpdir_create() from Unity's setUp() and tmpdir_destroy() from
 * tearDown(): tearDown() still runs after a failed assertion, so the
 * directory is removed then too.
 *
 * Two more hooks cover other endings: the directory is also removed at
 * exit(), and after SIGINT/SIGTERM the next tmpdir_destroy() removes it and
 * exits with status 128 + signal. A crash, an ASan abort or SIGKILL still
 * leaves it behind.
 */
#ifndef TMPDIR_H
#define TMPDIR_H

#include <stddef.h>

/**
 * @brief Create a fresh, empty temporary directory.
 *
 * Any directory left from a previous call is not removed; call
 * tmpdir_destroy() first.
 */
void tmpdir_create(void);

/**
 * @brief Remove the temporary directory and everything inside it.
 *
 * Does nothing if no directory is active, so it is safe to call twice.
 */
void tmpdir_destroy(void);

/**
 * @brief Write @p content to a file called @p name inside the temporary directory.
 *
 * An existing file with the same name is overwritten.
 *
 * @param name    File name, relative to the directory (no subdirectories).
 * @param content NUL-terminated text to write; use "" for an empty file.
 * @return Full path of the file, in a static buffer that the next call
 *         overwrites; NULL if the directory is missing or the write failed.
 */
const char *tmpdir_write(const char *name, const char *content);

/**
 * @brief Like tmpdir_write(), but for @p len bytes that may contain NUL.
 *
 * @param name Name of the file to create, as in tmpdir_write(); a name with a
 *             '/' is refused (NULL) so that nothing is written outside the directory.
 * @param data Bytes to write.
 * @param len  Number of bytes in @p data.
 * @return Full path of the file, with the same rules as tmpdir_write().
 */
const char *tmpdir_write_bytes(const char *name, const char *data, size_t len);

/**
 * @brief Full path of @p name inside the temporary directory, without creating it.
 *
 * Useful for paths that must not exist (for example a missing file).
 *
 * @param name Entry name, relative to the directory.
 * @return Path in a static buffer that the next call overwrites; NULL if no
 *         directory is active.
 */
const char *tmpdir_path(const char *name);

#endif
