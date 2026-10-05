/**
 * @file keys.c
 * @brief Implementation of keys.h. Public symbols are documented there.
 */
#include "keys.h"

#define ESC 0x1b

/** States of the decoder. */
enum {
	ST_NORMAL, /**< Outside any escape sequence. */
	ST_ESC,    /**< After ESC, waiting for '['. */
	ST_CSI     /**< After ESC [, reading parameter bytes until a final byte. */
};

void key_parser_init(key_parser_t *p) {
	p->state = ST_NORMAL;
	p->has_params = 0;
	p->is_delete = 0;
}

/** @brief Key for the final byte of a parameterless CSI sequence, or KEY_NONE. */
static int csi_key(unsigned char final) {
	switch (final) {
	case 'A': return KEY_UP;
	case 'B': return KEY_DOWN;
	case 'C': return KEY_RIGHT;
	case 'D': return KEY_LEFT;
	default: return KEY_NONE;
	}
}

int key_parser_feed(key_parser_t *p, unsigned char c) {
	switch (p->state) {
	case ST_NORMAL:
		if (c != ESC) return c;
		p->state = ST_ESC;
		return KEY_NONE;
	case ST_ESC:
		if (c == '[') {
			p->state = ST_CSI;
			p->has_params = 0;
			p->is_delete = 0;
		} else if (c != ESC) {
			p->state = ST_NORMAL; /* ESC followed by another byte: swallowed */
		}
		return KEY_NONE;
	default: /* ST_CSI */
		if (c == ESC) {
			p->state = ST_ESC;
		} else if (c >= 0x20 && c <= 0x3f) { /* parameter or intermediate byte */
			p->is_delete = !p->has_params && c == '3';
			p->has_params = 1;
		} else {
			p->state = ST_NORMAL; /* final byte, or a byte that aborts the sequence */
			if (c == '~' && p->is_delete) return KEY_DELETE;
			if (c >= 0x40 && c <= 0x7e && !p->has_params) return csi_key(c);
		}
		return KEY_NONE;
	}
}

int key_parser_pending(const key_parser_t *p) {
	return p->state != ST_NORMAL;
}

int key_parser_timeout(key_parser_t *p) {
	int was_lone_escape = p->state == ST_ESC;
	p->state = ST_NORMAL; /* a lone ESC is a key, a longer sequence is abandoned */
	return was_lone_escape ? KEY_ESC : KEY_NONE;
}
