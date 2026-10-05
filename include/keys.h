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
	KEY_LEFT,     /**< Arrow left: ESC [ D. */
	KEY_ESC,      /**< A lone Esc: ESC with nothing after it for KEY_ESC_TIMEOUT_MS. */
	KEY_DELETE    /**< The Delete key: ESC [ 3 ~. */
};

/** How long to wait for the rest of an escape sequence after ESC, in milliseconds. */
#define KEY_ESC_TIMEOUT_MS 50

/** Decoder state between bytes; start it with key_parser_init(). */
typedef struct {
	int state;      /**< Where in an escape sequence the decoder is. */
	int has_params; /**< Non-zero if the current CSI sequence has parameter bytes. */
	int is_delete;  /**< Non-zero if the parameter bytes so far are exactly "3". */
} key_parser_t;

/**
 * @brief Put @p p in its initial state, outside any escape sequence.
 * @param p Parser to initialise; must not be NULL.
 */
void key_parser_init(key_parser_t *p);

/**
 * @brief Feed one input byte to the decoder.
 *
 * ESC [ A/B/C/D give KEY_UP/KEY_DOWN/KEY_RIGHT/KEY_LEFT and ESC [ 3 ~ gives
 * KEY_DELETE. Any other escape
 * sequence (with parameters such as ESC [ 5 ~, or ESC followed by a byte
 * other than '[') is swallowed. A second ESC restarts a sequence. A lone ESC
 * cannot be told apart from the start of a sequence, so it is only reported
 * as KEY_NONE until key_parser_timeout() says that nothing else is coming.
 *
 * @param p Parser state; must not be NULL.
 * @param c Next byte read from the terminal.
 * @return The key (0-255 for an ordinary byte, or a KEY_ constant), or
 *         KEY_NONE if more bytes are needed or the sequence was swallowed.
 */
int key_parser_feed(key_parser_t *p, unsigned char c);

/**
 * @brief Tell whether the decoder is in the middle of an escape sequence.
 *
 * While it is, the caller should wait for the next byte for at most
 * KEY_ESC_TIMEOUT_MS and call key_parser_timeout() if none arrives; otherwise
 * it can wait without limit.
 *
 * @param p Parser state; must not be NULL.
 * @return Non-zero after ESC or ESC [ (and parameters); 0 otherwise.
 */
int key_parser_pending(const key_parser_t *p);

/**
 * @brief Report that no byte arrived within KEY_ESC_TIMEOUT_MS.
 *
 * After a lone ESC the key is KEY_ESC. In the middle of a longer sequence
 * (ESC [ ...) the sequence is abandoned. Either way the decoder is back in
 * its initial state.
 *
 * @param p Parser state; must not be NULL.
 * @return KEY_ESC after a lone ESC; KEY_NONE otherwise, including when the
 *         decoder was idle.
 */
int key_parser_timeout(key_parser_t *p);

#endif
