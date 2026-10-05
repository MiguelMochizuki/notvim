/**
 * @file cmdline.c
 * @brief Implementation of cmdline.h. Public symbols are documented there.
 */
#include <string.h>
#include "cmdline.h"
#include "utf8.h"

void cmdline_clear(cmdline_t *c) {
	c->text[0] = '\0';
	c->len = 0;
}

int cmdline_append(cmdline_t *c, const char *s, size_t n) {
	if (n > CMDLINE_MAX - c->len) return -1;
	memcpy(c->text + c->len, s, n);
	c->len += n;
	c->text[c->len] = '\0';
	return 0;
}

void cmdline_backspace(cmdline_t *c) {
	c->len = utf8_prev(c->text, c->len);
	c->text[c->len] = '\0';
}

/** @brief Whether @p c is an ASCII letter. */
static int is_letter(char c) {
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

void cmd_parse(const char *text, cmd_t *out) {
	const char *p = text + strspn(text, " :");
	size_t n = 0;
	while (is_letter(p[n])) n++;
	memcpy(out->name, p, n);
	out->name[n] = '\0';
	p += n;
	out->bang = *p == '!';
	p += out->bang;
	p += strspn(p, " ");
	n = strlen(p);
	while (n > 0 && p[n - 1] == ' ') n--;
	memcpy(out->arg, p, n);
	out->arg[n] = '\0';
}
