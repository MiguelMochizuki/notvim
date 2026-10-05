/**
 * @file keys.h
 * @brief Decoding of terminal input bytes into keys, including arrow keys.
 */
#ifndef KEYS_H
#define KEYS_H

/** Returned by key_parser_feed() when the byte does not complete a key yet. */
#define KEY_NONE (-1)

/** Keys that are not a single byte. Ordinary bytes are returned as their value (0-255). */
enum {
	KEY_UP = 256, /**< Arrow up: ESC [ A. */
	KEY_DOWN,     /**< Arrow down: ESC [ B. */
	KEY_RIGHT,    /**< Arrow right: ESC [ C. */
	KEY_LEFT      /**< Arrow left: ESC [ D. */
};

/** Decoder state between bytes; start it with key_parser_init(). */
typedef struct {
	int state;      /**< Where in an escape sequence the decoder is. */
	int has_params; /**< Non-zero if the current CSI sequence has parameter bytes. */
} key_parser_t;

/**
 * @brief Put @p p in its initial state, outside any escape sequence.
 * @param p Parser to initialise; must not be NULL.
 */
void key_parser_init(key_parser_t *p);

/**
 * @brief Feed one input byte to the decoder.
 *
 * ESC [ A/B/C/D give KEY_UP/KEY_DOWN/KEY_RIGHT/KEY_LEFT. Any other escape
 * sequence (with parameters such as ESC [ 3 ~, or ESC followed by a byte
 * other than '[') is swallowed. A second ESC restarts a sequence. A lone ESC
 * cannot be told apart from the start of a sequence, so it is only reported
 * as KEY_NONE.
 *
 * @param p Parser state; must not be NULL.
 * @param c Next byte read from the terminal.
 * @return The key (0-255 for an ordinary byte, or a KEY_ constant), or
 *         KEY_NONE if more bytes are needed or the sequence was swallowed.
 */
int key_parser_feed(key_parser_t *p, unsigned char c);

#endif
