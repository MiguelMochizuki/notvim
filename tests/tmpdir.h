/**
 * @file tmpdir.h
 * @brief Per-test temporary directory for tests that read or write files.
 *
 * Call tmpdir_create() from Unity's setUp() and tmpdir_destroy() from
 * tearDown(): tearDown() still runs after a failed assertion, so the
 * directory is always removed.
 */
#ifndef TMPDIR_H
#define TMPDIR_H

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
