/* Header for editor struct */
#ifndef EDITOR_H
#define EDITOR_H

#include <termios.h>

int editor_version(void);

/* Exposed for testing; not meant to be called outside editor.c */
void editor_set_raw_flags(struct termios *t);

void editor_enter_raw(void);
void editor_leave_raw(void);
int editor_should_exit(char c);

#endif
