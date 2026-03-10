#include <unity.h>
#include "LogEntry.h"
#include <string.h>
#include <stdio.h>

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

static LogEntry makeEntry(uint32_t ts, LogLevel lvl,
                          const char* comp, const char* msg) {
    LogEntry e;
    e.timestampMs = ts;
    e.level       = lvl;
    strncpy(e.component, comp, sizeof(e.component) - 1);
    e.component[sizeof(e.component) - 1] = '\0';
    strncpy(e.message, msg, sizeof(e.message) - 1);
    e.message[sizeof(e.message) - 1] = '\0';
    return e;
}

// ─────────────────────────────────────────────────────────────────────────────
// levelToString
// ─────────────────────────────────────────────────────────────────────────────

void test_levelToString_all_levels(void) {
    // All strings must be exactly 5 chars (padded) — toText uses %-5s
    TEST_ASSERT_EQUAL_STRING("DEBUG", LogEntry::levelToString(LogLevel::DEBUG));
    TEST_ASSERT_EQUAL_STRING("INFO ", LogEntry::levelToString(LogLevel::INFO));
    TEST_ASSERT_EQUAL_STRING("WARN ", LogEntry::levelToString(LogLevel::WARNING));
    TEST_ASSERT_EQUAL_STRING("ERROR", LogEntry::levelToString(LogLevel::ERROR));
    TEST_ASSERT_EQUAL_STRING("FATAL", LogEntry::levelToString(LogLevel::FATAL));
}

void test_levelToChar_all_levels(void) {
    TEST_ASSERT_EQUAL_STRING("D", LogEntry::levelToChar(LogLevel::DEBUG));
    TEST_ASSERT_EQUAL_STRING("I", LogEntry::levelToChar(LogLevel::INFO));
    TEST_ASSERT_EQUAL_STRING("W", LogEntry::levelToChar(LogLevel::WARNING));
    TEST_ASSERT_EQUAL_STRING("E", LogEntry::levelToChar(LogLevel::ERROR));
    TEST_ASSERT_EQUAL_STRING("F", LogEntry::levelToChar(LogLevel::FATAL));
}

// ─────────────────────────────────────────────────────────────────────────────
// toText
// ─────────────────────────────────────────────────────────────────────────────

void test_toText_format_contains_timestamp(void) {
    LogEntry e = makeEntry(1289, LogLevel::INFO, "LDC1101", "hello");
    char buf[256];
    e.toText(buf, sizeof(buf));
    // Must contain the timestamp value
    TEST_ASSERT_NOT_NULL(strstr(buf, "1289"));
}

void test_toText_format_contains_level(void) {
    LogEntry e = makeEntry(0, LogLevel::WARNING, "Sys", "msg");
    char buf[256];
    e.toText(buf, sizeof(buf));
    TEST_ASSERT_NOT_NULL(strstr(buf, "WARN"));
}

void test_toText_format_contains_component(void) {
    LogEntry e = makeEntry(0, LogLevel::INFO, "LDC1101", "msg");
    char buf[256];
    e.toText(buf, sizeof(buf));
    TEST_ASSERT_NOT_NULL(strstr(buf, "LDC1101"));
}

void test_toText_format_contains_message(void) {
    LogEntry e = makeEntry(0, LogLevel::INFO, "Sys", "CoinTrace starting");
    char buf[256];
    e.toText(buf, sizeof(buf));
    TEST_ASSERT_NOT_NULL(strstr(buf, "CoinTrace starting"));
}

void test_toText_format_full_pattern(void) {
    // Verify the full format: [   123ms] INFO  System         | hello\n
    LogEntry e = makeEntry(123, LogLevel::INFO, "System", "hello");
    char buf[256];
    e.toText(buf, sizeof(buf));

    // Must start with '['
    TEST_ASSERT_EQUAL_CHAR('[', buf[0]);
    // Must end with '\n'
    size_t len = strlen(buf);
    TEST_ASSERT_EQUAL_CHAR('\n', buf[len - 1]);
    // Must contain separator
    TEST_ASSERT_NOT_NULL(strstr(buf, "| hello"));
}

void test_toText_component_truncated_to_19_chars(void) {
    // component field is 20 bytes → max 19 printable chars
    LogEntry e = makeEntry(0, LogLevel::INFO,
                            "ThisNameIs19Chars__X",  // 20 chars → truncated to 19
                            "msg");
    // strncpy copies 19 chars, null at [19]
    TEST_ASSERT_EQUAL_INT('\0', e.component[19]);
    TEST_ASSERT_EQUAL_INT(19, (int)strlen(e.component));
}

// ─────────────────────────────────────────────────────────────────────────────
// toJSON
// ─────────────────────────────────────────────────────────────────────────────

void test_toJSON_contains_required_keys(void) {
    LogEntry e = makeEntry(500, LogLevel::ERROR, "Sensor", "err42");
    char buf[320];
    e.toJSON(buf, sizeof(buf));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"t\":500"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"l\":3"));   // ERROR = 3
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"c\":\"Sensor\""));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"m\":\"err42\""));
}

void test_toJSON_ends_with_newline(void) {
    LogEntry e = makeEntry(0, LogLevel::INFO, "X", "y");
    char buf[256];
    e.toJSON(buf, sizeof(buf));
    size_t len = strlen(buf);
    TEST_ASSERT_EQUAL_CHAR('\n', buf[len - 1]);
}

// ─────────────────────────────────────────────────────────────────────────────
// toBLECompact
// ─────────────────────────────────────────────────────────────────────────────

void test_toBLECompact_format(void) {
    // Format: levelChar|ts|component|message
    LogEntry e = makeEntry(42, LogLevel::WARNING, "BLE", "lost");
    char buf[128];
    e.toBLECompact(buf, sizeof(buf));
    TEST_ASSERT_NOT_NULL(strstr(buf, "W|42|BLE|lost"));
}

// ─────────────────────────────────────────────────────────────────────────────
// Runner
// ─────────────────────────────────────────────────────────────────────────────

void setUp(void)    {}
void tearDown(void) {}

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_levelToString_all_levels);
    RUN_TEST(test_levelToChar_all_levels);

    RUN_TEST(test_toText_format_contains_timestamp);
    RUN_TEST(test_toText_format_contains_level);
    RUN_TEST(test_toText_format_contains_component);
    RUN_TEST(test_toText_format_contains_message);
    RUN_TEST(test_toText_format_full_pattern);
    RUN_TEST(test_toText_component_truncated_to_19_chars);

    RUN_TEST(test_toJSON_contains_required_keys);
    RUN_TEST(test_toJSON_ends_with_newline);

    RUN_TEST(test_toBLECompact_format);

    return UNITY_END();
}
