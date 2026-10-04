/* Header for editor struct */
#ifndef EDITOR_H
#define EDITOR_H

#include <stddef.h>
#include <termios.h>

#define EDITOR_BUFFER_SIZE 1024

typedef struct {
	char buffer[EDITOR_BUFFER_SIZE];
	size_t len;
} editor_t;

int editor_version(void);

/* Exposed for testing; not meant to be called outside editor.c */
void editor_set_raw_flags(struct termios *t);

void editor_enter_raw(int fd);
void editor_leave_raw(int fd);
int editor_should_exit(char c);
void editor_init(editor_t *e);
size_t editor_render(const editor_t *e, char *out, size_t out_size);

#endif
