/**
 * @file test_keys.c
 * @brief Unit tests for the key decoder in keys.c.
 */
#include <string.h>
#include "unity.h"
#include "test_keys.h"
#include "keys.h"

/**
 * @brief Feed every byte of @p bytes to @p p and return the result of the last one.
 *
 * Asserts that all earlier bytes returned KEY_NONE, since they cannot
 * complete a key.
 */
static int feed(key_parser_t *p, const char *bytes) {
	size_t n = strlen(bytes);
	for (size_t i = 0; i + 1 < n; i++) {
		TEST_ASSERT_EQUAL_INT(KEY_NONE, key_parser_feed(p, (unsigned char)bytes[i]));
	}
	return key_parser_feed(p, (unsigned char)bytes[n - 1]);
}

/** @brief An ordinary byte is returned as itself. */
static void test_keys_plain_byte_passes_through(void) {
	key_parser_t p;
	key_parser_init(&p);
	TEST_ASSERT_EQUAL_INT('a', feed(&p, "a"));
	TEST_ASSERT_EQUAL_INT(0x11, feed(&p, "\x11")); /* Ctrl+Q */
}

/** @brief ESC [ A, B, C and D are the four arrow keys. */
static void test_keys_arrows_are_decoded(void) {
	key_parser_t p;
	key_parser_init(&p);
	TEST_ASSERT_EQUAL_INT(KEY_UP, feed(&p, "\x1b[A"));
	TEST_ASSERT_EQUAL_INT(KEY_DOWN, feed(&p, "\x1b[B"));
	TEST_ASSERT_EQUAL_INT(KEY_RIGHT, feed(&p, "\x1b[C"));
	TEST_ASSERT_EQUAL_INT(KEY_LEFT, feed(&p, "\x1b[D"));
}

/** @brief A lone ESC is not a key yet: the decoder waits for more bytes. */
static void test_keys_lone_escape_returns_none(void) {
	key_parser_t p;
	key_parser_init(&p);
	TEST_ASSERT_EQUAL_INT(KEY_NONE, feed(&p, "\x1b"));
}

/** @brief A sequence with parameters (ESC [ 3 ~) is swallowed and the next key still works. */
static void test_keys_sequence_with_params_is_swallowed(void) {
	key_parser_t p;
	key_parser_init(&p);
	TEST_ASSERT_EQUAL_INT(KEY_NONE, feed(&p, "\x1b[3~"));
	TEST_ASSERT_EQUAL_INT('a', feed(&p, "a"));
}

/** @brief Ctrl+right (ESC [ 1 ; 5 C) has parameters, so it is not an arrow. */
static void test_keys_arrow_with_params_is_swallowed(void) {
	key_parser_t p;
	key_parser_init(&p);
	TEST_ASSERT_EQUAL_INT(KEY_NONE, feed(&p, "\x1b[1;5C"));
	TEST_ASSERT_EQUAL_INT(KEY_UP, feed(&p, "\x1b[A"));
}

/** @brief An unknown final byte (ESC [ Z) is swallowed. */
static void test_keys_unknown_final_byte_is_swallowed(void) {
	key_parser_t p;
	key_parser_init(&p);
	TEST_ASSERT_EQUAL_INT(KEY_NONE, feed(&p, "\x1b[Z"));
	TEST_ASSERT_EQUAL_INT('a', feed(&p, "a"));
}

/** @brief ESC followed by something other than '[' is swallowed, not turned into that byte. */
static void test_keys_escape_then_other_byte_is_swallowed(void) {
	key_parser_t p;
	key_parser_init(&p);
	TEST_ASSERT_EQUAL_INT(KEY_NONE, feed(&p, "\x1bx"));
	TEST_ASSERT_EQUAL_INT('a', feed(&p, "a"));
}

/** @brief A second ESC restarts the sequence, also inside "ESC [". */
static void test_keys_second_escape_restarts_the_sequence(void) {
	key_parser_t p;
	key_parser_init(&p);
	TEST_ASSERT_EQUAL_INT(KEY_UP, feed(&p, "\x1b\x1b[A"));
	TEST_ASSERT_EQUAL_INT(KEY_DOWN, feed(&p, "\x1b[\x1b[B"));
}

/** @brief A control byte inside a sequence aborts it and the next key still works. */
static void test_keys_control_byte_inside_sequence_aborts_it(void) {
	key_parser_t p;
	key_parser_init(&p);
	TEST_ASSERT_EQUAL_INT(KEY_NONE, feed(&p, "\x1b[\x01"));
	TEST_ASSERT_EQUAL_INT('a', feed(&p, "a"));
}

/** @brief Register every test in this file with Unity. */
void test_keys_suite(void) {
	RUN_TEST(test_keys_plain_byte_passes_through);
	RUN_TEST(test_keys_arrows_are_decoded);
	RUN_TEST(test_keys_lone_escape_returns_none);
	RUN_TEST(test_keys_sequence_with_params_is_swallowed);
	RUN_TEST(test_keys_arrow_with_params_is_swallowed);
	RUN_TEST(test_keys_unknown_final_byte_is_swallowed);
	RUN_TEST(test_keys_escape_then_other_byte_is_swallowed);
	RUN_TEST(test_keys_second_escape_restarts_the_sequence);
	RUN_TEST(test_keys_control_byte_inside_sequence_aborts_it);
}
