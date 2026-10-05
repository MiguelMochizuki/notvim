/**
 * @file commands.h
 * @brief The ':' commands: what Enter on the command line does.
 */
#ifndef COMMANDS_H
#define COMMANDS_H

#include "editor.h"

/**
 * @brief Run the command @p text (as typed after ':') on @p e and leave its result in the message.
 *
 * An empty command (only spaces and colons) does nothing. "w", "w!" and "w name" write the text with
 * editor_write_file(): to the path of @p e, or to the name given, which becomes the path if @p e has none. Without a
 * path and without a name the message is "E32: No file name". Success sets the message
 * "\"name\" [New] 12L, 345B written" ("[New]" when the file did not exist, "[dos]" after it for a CRLF file) and,
 * when the file written is the path of @p e, clears @c modified (as in Vim, writing to another name does not). A failure
 * leaves @c modified alone and sets a message with the strerror() text. Any other command is
 * "E492: Not an editor command: <text>" for now.
 *
 * @param e    Editor to act on; must not be NULL.
 * @param text NUL-terminated command text, at most CMDLINE_MAX bytes.
 */
void commands_run(editor_t *e, const char *text);

#endif
