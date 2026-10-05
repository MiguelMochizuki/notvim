/**
 * @file writer.c
 * @brief Implementation of writer.h.
 */
#define _XOPEN_SOURCE 700 /* mkstemp, realpath, fsync, fchmod, fdopen under -std=c11 */
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "writer.h"

/** @brief Write the lines of @p e to @p f; return 0 or -1. Counts the bytes in *bytes. */
static int put_lines(const editor_t *e, FILE *f, size_t *bytes) {
	const char *eol = e->crlf ? "\r\n" : "\n";
	size_t eol_len = strlen(eol);
	*bytes = 0;
	for (size_t i = 0; i < e->count; i++) {
		size_t n = strlen(e->lines[i]);
		if (fwrite(e->lines[i], 1, n, f) != n || fwrite(eol, 1, eol_len, f) != eol_len) return -1;
		*bytes += n + eol_len;
	}
	return 0;
}

int editor_write_file(const editor_t *e, const char *path, size_t *lines, size_t *bytes) {
	char real[PATH_MAX], tmp[PATH_MAX + 32];
	struct stat st;
	if (lstat(path, &st) == 0 && S_ISLNK(st.st_mode)) { /* write through a link: replace what it points to */
		if (!realpath(path, real)) return -1;
		path = real;
	}
	mode_t mode;
	if (stat(path, &st) == 0) mode = st.st_mode & 07777;
	else if (errno == ENOENT) {
		mode_t um = umask(0);
		umask(um); /* ponytail: umask can only be read by setting it; fine for a single thread */
		mode = 0666 & ~um;
	} else return -1;
	const char *slash = strrchr(path, '/');
	int dirlen = slash ? (int)(slash - path) + 1 : 0;
	if (snprintf(tmp, sizeof(tmp), "%.*s.notvim.XXXXXX", dirlen, path) >= (int)sizeof(tmp)) {
		errno = ENAMETOOLONG;
		return -1;
	}
	int fd = mkstemp(tmp); /* created exclusively, mode 0600 */
	if (fd < 0) return -1;
	FILE *f = fdopen(fd, "wb");
	if (!f) {
		int err = errno;
		close(fd);
		unlink(tmp);
		errno = err;
		return -1;
	}
	size_t n = 0;
	int bad = put_lines(e, f, &n) < 0 || fflush(f) != 0 || fchmod(fd, mode) != 0 || fsync(fd) != 0;
	int err = errno;
	if (fclose(f) != 0 && !bad) bad = 1, err = errno;
	if (!bad && rename(tmp, path) != 0) bad = 1, err = errno;
	if (bad) {
		unlink(tmp);
		errno = err;
		return -1;
	}
	if (lines) *lines = e->count;
	if (bytes) *bytes = n;
	return 0;
}
