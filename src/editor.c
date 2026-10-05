/**
 * @file editor.c
 * @brief Implementation of editor.h. Public symbols are documented there.
 */
#include <string.h>
#include "editor.h"

int editor_should_exit(char c) {
	return c == 0x11; /* Ctrl+Q = DC1 */
}

void editor_init(editor_t *e) {
	memset(e->buffer, 0, sizeof(e->buffer));
	e->len = 0;
}

size_t editor_render(const editor_t *e, char *out, size_t out_size) {
	if (out_size == 0) return 0;
	size_t n = e->len < out_size - 1 ? e->len : out_size - 1;
	memcpy(out, e->buffer, n);
	out[n] = '\0';
	return n;
}
