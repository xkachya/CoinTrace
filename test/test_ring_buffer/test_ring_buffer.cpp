#include <unity.h>
#include "RingBufferTransport.h"
#include <string.h>

// g_mock_millis defined in mock_impl.cpp
extern uint32_t g_mock_millis;

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

static LogEntry makeEntry(uint32_t ts, LogLevel lvl, const char* comp, const char* msg) {
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
// Empty ring
// ─────────────────────────────────────────────────────────────────────────────

void test_ring_empty_after_begin(void) {
    RingBufferTransport ring(5, false);
    ring.begin();
    TEST_ASSERT_EQUAL_UINT16(0, ring.getCount());
}

void test_ring_getEntries_empty_returns_zero(void) {
    RingBufferTransport ring(5, false);
    ring.begin();
    LogEntry buf[5];
    TEST_ASSERT_EQUAL_UINT16(0, ring.getEntries(buf, 5));
}

void test_ring_getLastError_empty_returns_false(void) {
    RingBufferTransport ring(5, false);
    ring.begin();
    LogEntry out;
    TEST_ASSERT_FALSE(ring.getLastError(out));
}

// ─────────────────────────────────────────────────────────────────────────────
// Single write
// ─────────────────────────────────────────────────────────────────────────────

void test_ring_write_one_increments_count(void) {
    RingBufferTransport ring(5, false);
    ring.begin();
    ring.write(makeEntry(1, LogLevel::INFO, "Sys", "hello"));
    TEST_ASSERT_EQUAL_UINT16(1, ring.getCount());
}

void test_ring_getEntries_returns_correct_content(void) {
    RingBufferTransport ring(5, false);
    ring.begin();
    ring.write(makeEntry(42, LogLevel::ERROR, "LDC1101", "chip fail"));

    LogEntry buf[1];
    uint16_t n = ring.getEntries(buf, 1);
    TEST_ASSERT_EQUAL_UINT16(1, n);
    TEST_ASSERT_EQUAL_UINT32(42, buf[0].timestampMs);
    TEST_ASSERT_EQUAL(LogLevel::ERROR, buf[0].level);
    TEST_ASSERT_EQUAL_STRING("LDC1101", buf[0].component);
    TEST_ASSERT_EQUAL_STRING("chip fail", buf[0].message);
}

// ─────────────────────────────────────────────────────────────────────────────
// Fill to capacity (no wrap yet)
// ─────────────────────────────────────────────────────────────────────────────

void test_ring_fill_to_capacity(void) {
    RingBufferTransport ring(3, false);
    ring.begin();
    ring.write(makeEntry(1, LogLevel::INFO, "A", "one"));
    ring.write(makeEntry(2, LogLevel::INFO, "A", "two"));
    ring.write(makeEntry(3, LogLevel::INFO, "A", "three"));
    TEST_ASSERT_EQUAL_UINT16(3, ring.getCount());
}

// ─────────────────────────────────────────────────────────────────────────────
// Wrap-around: capacity=3, write 4 entries
// Entry[0] (ts=1) must be dropped; remaining oldest→newest: ts=2, ts=3, ts=4
// ─────────────────────────────────────────────────────────────────────────────

void test_ring_wrap_drops_oldest(void) {
    RingBufferTransport ring(3, false);
    ring.begin();
    ring.write(makeEntry(1, LogLevel::INFO, "A", "first"));   // will be dropped
    ring.write(makeEntry(2, LogLevel::INFO, "A", "second"));
    ring.write(makeEntry(3, LogLevel::INFO, "A", "third"));
    ring.write(makeEntry(4, LogLevel::INFO, "A", "fourth"));  // causes wrap

    TEST_ASSERT_EQUAL_UINT16(3, ring.getCount());  // still 3

    LogEntry buf[3];
    uint16_t n = ring.getEntries(buf, 3);
    TEST_ASSERT_EQUAL_UINT16(3, n);
    // Oldest-to-newest order after wrap
    TEST_ASSERT_EQUAL_UINT32(2, buf[0].timestampMs);
    TEST_ASSERT_EQUAL_UINT32(3, buf[1].timestampMs);
    TEST_ASSERT_EQUAL_UINT32(4, buf[2].timestampMs);
}

void test_ring_wrap_multiple_rounds(void) {
    // capacity=3, write 7 entries — ring wraps twice
    RingBufferTransport ring(3, false);
    ring.begin();
    for (uint32_t i = 1; i <= 7; i++) {
        ring.write(makeEntry(i, LogLevel::INFO, "A", "m"));
    }
    TEST_ASSERT_EQUAL_UINT16(3, ring.getCount());

    LogEntry buf[3];
    ring.getEntries(buf, 3);
    // Last 3 entries survive: ts=5, ts=6, ts=7
    TEST_ASSERT_EQUAL_UINT32(5, buf[0].timestampMs);
    TEST_ASSERT_EQUAL_UINT32(6, buf[1].timestampMs);
    TEST_ASSERT_EQUAL_UINT32(7, buf[2].timestampMs);
}

// ─────────────────────────────────────────────────────────────────────────────
// getEntries minLevel filter
// ─────────────────────────────────────────────────────────────────────────────

void test_ring_getEntries_minLevel_filters(void) {
    RingBufferTransport ring(5, false);
    ring.begin();
    ring.write(makeEntry(1, LogLevel::DEBUG,   "A", "dbg"));
    ring.write(makeEntry(2, LogLevel::INFO,    "A", "inf"));
    ring.write(makeEntry(3, LogLevel::WARNING, "A", "wrn"));
    ring.write(makeEntry(4, LogLevel::ERROR,   "A", "err"));

    LogEntry buf[5];
    uint16_t n = ring.getEntries(buf, 5, LogLevel::WARNING);
    TEST_ASSERT_EQUAL_UINT16(2, n);  // WARNING + ERROR
    TEST_ASSERT_EQUAL(LogLevel::WARNING, buf[0].level);
    TEST_ASSERT_EQUAL(LogLevel::ERROR,   buf[1].level);
}

// ─────────────────────────────────────────────────────────────────────────────
// getLastError — returns most recent ERROR/FATAL
// ─────────────────────────────────────────────────────────────────────────────

void test_ring_getLastError_no_errors_returns_false(void) {
    RingBufferTransport ring(5, false);
    ring.begin();
    ring.write(makeEntry(1, LogLevel::INFO,    "A", "ok"));
    ring.write(makeEntry(2, LogLevel::WARNING, "A", "warn"));
    LogEntry out;
    TEST_ASSERT_FALSE(ring.getLastError(out));
}

void test_ring_getLastError_returns_single_error(void) {
    RingBufferTransport ring(5, false);
    ring.begin();
    ring.write(makeEntry(10, LogLevel::ERROR, "LDC1101", "chip fail"));
    LogEntry out;
    TEST_ASSERT_TRUE(ring.getLastError(out));
    TEST_ASSERT_EQUAL_UINT32(10, out.timestampMs);
    TEST_ASSERT_EQUAL_STRING("chip fail", out.message);
}

void test_ring_getLastError_returns_most_recent(void) {
    // Three errors — must return last one (ts=30)
    RingBufferTransport ring(10, false);
    ring.begin();
    ring.write(makeEntry(10, LogLevel::ERROR, "A", "first err"));
    ring.write(makeEntry(20, LogLevel::INFO,  "A", "ok"));
    ring.write(makeEntry(30, LogLevel::ERROR, "A", "last err"));
    ring.write(makeEntry(40, LogLevel::INFO,  "A", "ok2"));

    LogEntry out;
    TEST_ASSERT_TRUE(ring.getLastError(out));
    TEST_ASSERT_EQUAL_UINT32(30, out.timestampMs);
    TEST_ASSERT_EQUAL_STRING("last err", out.message);
}

void test_ring_getLastError_includes_fatal(void) {
    RingBufferTransport ring(5, false);
    ring.begin();
    ring.write(makeEntry(1, LogLevel::ERROR, "A", "err"));
    ring.write(makeEntry(2, LogLevel::FATAL, "A", "crash"));
    LogEntry out;
    ring.getLastError(out);
    TEST_ASSERT_EQUAL(LogLevel::FATAL, out.level);
    TEST_ASSERT_EQUAL_UINT32(2, out.timestampMs);
}

// ─────────────────────────────────────────────────────────────────────────────
// clear()
// ─────────────────────────────────────────────────────────────────────────────

void test_ring_clear_resets_count(void) {
    RingBufferTransport ring(5, false);
    ring.begin();
    ring.write(makeEntry(1, LogLevel::INFO, "A", "m"));
    ring.write(makeEntry(2, LogLevel::INFO, "A", "m"));
    ring.clear();
    TEST_ASSERT_EQUAL_UINT16(0, ring.getCount());
}

void test_ring_clear_then_write_works(void) {
    RingBufferTransport ring(3, false);
    ring.begin();
    ring.write(makeEntry(1, LogLevel::INFO, "A", "old"));
    ring.write(makeEntry(2, LogLevel::INFO, "A", "old"));
    ring.clear();
    ring.write(makeEntry(99, LogLevel::WARNING, "B", "new"));

    LogEntry buf[3];
    uint16_t n = ring.getEntries(buf, 3);
    TEST_ASSERT_EQUAL_UINT16(1, n);
    TEST_ASSERT_EQUAL_UINT32(99, buf[0].timestampMs);
}

// ─────────────────────────────────────────────────────────────────────────────
// getEntries respects maxCount
// ─────────────────────────────────────────────────────────────────────────────

void test_ring_getEntries_respects_maxCount(void) {
    RingBufferTransport ring(10, false);
    ring.begin();
    for (int i = 0; i < 8; i++) {
        ring.write(makeEntry(i, LogLevel::INFO, "A", "m"));
    }
    LogEntry buf[3];
    uint16_t n = ring.getEntries(buf, 3);
    TEST_ASSERT_EQUAL_UINT16(3, n);
}

// ─────────────────────────────────────────────────────────────────────────────
// Runner
// ─────────────────────────────────────────────────────────────────────────────

void setUp(void)    { g_mock_millis = 0; }
void tearDown(void) {}

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_ring_empty_after_begin);
    RUN_TEST(test_ring_getEntries_empty_returns_zero);
    RUN_TEST(test_ring_getLastError_empty_returns_false);

    RUN_TEST(test_ring_write_one_increments_count);
    RUN_TEST(test_ring_getEntries_returns_correct_content);

    RUN_TEST(test_ring_fill_to_capacity);
    RUN_TEST(test_ring_wrap_drops_oldest);
    RUN_TEST(test_ring_wrap_multiple_rounds);

    RUN_TEST(test_ring_getEntries_minLevel_filters);

    RUN_TEST(test_ring_getLastError_no_errors_returns_false);
    RUN_TEST(test_ring_getLastError_returns_single_error);
    RUN_TEST(test_ring_getLastError_returns_most_recent);
    RUN_TEST(test_ring_getLastError_includes_fatal);

    RUN_TEST(test_ring_clear_resets_count);
    RUN_TEST(test_ring_clear_then_write_works);
    RUN_TEST(test_ring_getEntries_respects_maxCount);

    return UNITY_END();
}
