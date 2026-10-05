/**
 * @file commands.c
 * @brief Implementation of commands.h.
 */
#define _POSIX_C_SOURCE 200809L /* access under -std=c11 */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "commands.h"
#include "writer.h"

/** @brief Show "<text>" as the message; out of memory: no message. */
static void say(editor_t *e, const char *text) {
	editor_set_message(e, text);
}

/** @brief The ":w" command: write to the path of @p e or to the name in @p arg, and report; return 0 if it was written, -1 if not. */
static int cmd_write(editor_t *e, const char *arg) {
	const char *path = arg[0] ? arg : e->path;
	if (!path) {
		say(e, "E32: No file name");
		return -1;
	}
	int is_new = access(path, F_OK) != 0;
	size_t lines = 0, bytes = 0;
	char *msg = malloc(strlen(path) + 256);
	if (!msg) return -1;
	if (editor_write_file(e, path, &lines, &bytes) < 0) {
		sprintf(msg, "\"%s\" %s", path, strerror(errno));
		say(e, msg);
		free(msg);
		return -1;
	}
	if (!e->path) { /* adopt the name; the message is set after, as a load or free would drop it */
		e->path = strdup(path);
	}
	if (e->path && strcmp(e->path, path) == 0) e->modified = 0;
	sprintf(msg, "\"%s\"%s%s %zuL, %zuB written", path, is_new ? " [New]" : "", e->crlf ? " [dos]" : "", lines, bytes);
	say(e, msg);
	free(msg);
	return 0;
}

/** @brief The ":q" command: ask to quit, or refuse when there are unsaved changes and @p bang is not set. */
static void cmd_quit(editor_t *e, int bang) {
	if (e->modified && !bang) say(e, "E37: No write since last change (add ! to override)");
	else e->quit = 1;
}

void commands_run(editor_t *e, const char *text) {
	cmd_t cmd;
	cmd_parse(text, &cmd);
	if (strcmp(cmd.name, "w") == 0) {
		cmd_write(e, cmd.arg);
	} else if (strcmp(cmd.name, "q") == 0) {
		cmd_quit(e, cmd.bang);
	} else if (strcmp(cmd.name, "wq") == 0 || strcmp(cmd.name, "x") == 0) {
		/* ":wq" always writes, ":x" only a modified buffer */
		if ((cmd.name[0] == 'w' || e->modified) && cmd_write(e, cmd.arg) < 0) return;
		e->quit = 1;
	} else if (cmd.name[0] || cmd.bang || cmd.arg[0]) {
		char msg[CMDLINE_MAX + 40];
		snprintf(msg, sizeof(msg), "E492: Not an editor command: %s", text);
		say(e, msg);
	}
}
