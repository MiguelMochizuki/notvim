/**
 * @file writer.h
 * @brief Writing the text of an editor to a file, atomically.
 */
#ifndef WRITER_H
#define WRITER_H

#include <stddef.h>
#include "editor.h"

/**
 * @brief Write every line of @p e to the file @p path.
 *
 * Each line is followed by "\n", or by "\r\n" if @c e->crlf is set (a CRLF file loaded as such); nothing else is added
 * or changed, so a lone '\r' inside a line and invalid UTF-8 are written as stored, and a file that is not CRLF never
 * gets a CR. An editor with no lines gives an empty file, one empty line gives "\n"; a file loaded without a final
 * newline gets one, as in Vim.
 *
 * The write is atomic: the data goes to a temporary file created exclusively in the directory of the target, is
 * flushed with fsync() and then renamed over the target, so on any error the original is untouched and the temporary
 * file is removed. The permission bits of an existing target are kept (a new file gets 0666 minus the umask). If
 * @p path is a symbolic link the file it points to is replaced and the link stays. Hard links are lost by the rename.
 *
 * @param e     Editor to write; must not be NULL.
 * @param path  Path of the file to write.
 * @param lines Receives the number of lines written on success; may be NULL.
 * @param bytes Receives the number of bytes written on success; may be NULL.
 * @return 0 on success; -1 on any error, with errno set (EISDIR if @p path is a directory).
 */
int editor_write_file(const editor_t *e, const char *path, size_t *lines, size_t *bytes);

#endif
