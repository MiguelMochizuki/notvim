/**
 * Tests for main
 */
#include "unity.h"
#include "test_editor.h"

int main(void) {
    UNITY_BEGIN();
    test_editor_suite();
    return UNITY_END();
}
