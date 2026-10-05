/**
 * @file tmpdir.c
 * @brief Implementation of tmpdir.h. Public symbols are documented there.
 */
#define _XOPEN_SOURCE 700 /* mkdtemp, nftw under -std=c11 */
#include <ftw.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tmpdir.h"

/** Path of the active temporary directory; empty when there is none. */
static char dir[64];

/** @brief nftw() callback that removes each entry; used depth-first. */
static int remove_entry(const char *path, const struct stat *sb, int type, struct FTW *ftw) {
	(void)sb; (void)type; (void)ftw;
	return remove(path);
}

void tmpdir_create(void) {
	strcpy(dir, "/tmp/notvim_XXXXXX");
	if (mkdtemp(dir) == NULL) dir[0] = '\0';
}

void tmpdir_destroy(void) {
	if (dir[0]) nftw(dir, remove_entry, 16, FTW_DEPTH | FTW_PHYS);
	dir[0] = '\0';
}

const char *tmpdir_path(const char *name) {
	static char path[128];
	if (!dir[0]) return NULL;
	snprintf(path, sizeof(path), "%s/%s", dir, name);
	return path;
}

const char *tmpdir_write(const char *name, const char *content) {
	const char *path = tmpdir_path(name);
	if (!path) return NULL;
	FILE *f = fopen(path, "w");
	if (!f) return NULL;
	int failed = fputs(content, f) == EOF;
	if (fclose(f) == EOF) failed = 1;
	return failed ? NULL : path;
}
