/**
 * Tests for editor struct implementations
 */
#include "unity.h"
#include "test_editor.h"
#include "editor.h"

void setUp(void) {}
void tearDown(void) {}

static void test_editor_version_returns_zero(void) {
    TEST_ASSERT_EQUAL_INT(0, editor_version());
}

void test_editor_suite(void) {
    RUN_TEST(test_editor_version_returns_zero);
}
