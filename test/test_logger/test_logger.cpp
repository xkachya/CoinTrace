#include <unity.h>
#include "Logger.h"
#include "RingBufferTransport.h"
#include <string.h>

extern uint32_t g_mock_millis;

// ─────────────────────────────────────────────────────────────────────────────
// MockTransport — captures every write() call for assertion
// ─────────────────────────────────────────────────────────────────────────────

struct MockTransport : public ILogTransport {
    LogEntry entries[32];
    uint8_t  count   = 0;
    bool     begun   = false;

    bool        begin()  override { begun = true; return true; }
    void        write(const LogEntry& e) override {
        if (count < 32) entries[count++] = e;
    }
    const char* getName() const override { return "Mock"; }
};

// ─────────────────────────────────────────────────────────────────────────────
// begin() + addTransport
// ─────────────────────────────────────────────────────────────────────────────

void test_logger_begin_required_before_use(void) {
    Logger logger;
    TEST_ASSERT_TRUE(logger.begin());  // creates mutex — must succeed
}

void test_logger_addTransport_calls_begin(void) {
    Logger logger;
    logger.begin();
    MockTransport t;
    TEST_ASSERT_FALSE(t.begun);
    logger.addTransport(&t);
    TEST_ASSERT_TRUE(t.begun);
}

void test_logger_addTransport_max_four(void) {
    Logger logger;
    logger.begin();
    MockTransport t1, t2, t3, t4, t5;
    TEST_ASSERT_TRUE(logger.addTransport(&t1));
    TEST_ASSERT_TRUE(logger.addTransport(&t2));
    TEST_ASSERT_TRUE(logger.addTransport(&t3));
    TEST_ASSERT_TRUE(logger.addTransport(&t4));
    TEST_ASSERT_FALSE(logger.addTransport(&t5));  // 5th must fail
    TEST_ASSERT_EQUAL_UINT8(4, logger.getTransportCount());
}

void test_logger_addTransport_null_returns_false(void) {
    Logger logger;
    logger.begin();
    TEST_ASSERT_FALSE(logger.addTransport(nullptr));
}

// ─────────────────────────────────────────────────────────────────────────────
// Message dispatch
// ─────────────────────────────────────────────────────────────────────────────

void test_logger_dispatches_to_transport(void) {
    Logger logger;
    logger.begin();
    MockTransport t;
    logger.addTransport(&t);

    logger.info("System", "hello %s", "world");

    TEST_ASSERT_EQUAL_UINT8(1, t.count);
    TEST_ASSERT_EQUAL_STRING("System", t.entries[0].component);
    TEST_ASSERT_EQUAL_STRING("hello world", t.entries[0].message);
    TEST_ASSERT_EQUAL(LogLevel::INFO, t.entries[0].level);
}

void test_logger_dispatches_to_all_transports(void) {
    Logger logger;
    logger.begin();
    MockTransport t1, t2;
    logger.addTransport(&t1);
    logger.addTransport(&t2);

    logger.error("Sensor", "fail");

    TEST_ASSERT_EQUAL_UINT8(1, t1.count);
    TEST_ASSERT_EQUAL_UINT8(1, t2.count);
}

void test_logger_timestamp_comes_from_millis(void) {
    Logger logger;
    logger.begin();
    MockTransport t;
    logger.addTransport(&t);

    g_mock_millis = 1500;
    logger.info("A", "msg");

    TEST_ASSERT_EQUAL_UINT32(1500, t.entries[0].timestampMs);
}

// ─────────────────────────────────────────────────────────────────────────────
// globalMinLevel filter
// ─────────────────────────────────────────────────────────────────────────────

void test_logger_globalMinLevel_blocks_below(void) {
    Logger logger;
    logger.begin();
    logger.setGlobalMinLevel(LogLevel::WARNING);
    MockTransport t;
    logger.addTransport(&t);

    logger.debug("A", "dbg");    // below WARNING — must be dropped
    logger.info("A", "inf");     // below WARNING — must be dropped
    logger.warning("A", "wrn"); // exactly WARNING — must pass

    TEST_ASSERT_EQUAL_UINT8(1, t.count);
    TEST_ASSERT_EQUAL(LogLevel::WARNING, t.entries[0].level);
}

void test_logger_globalMinLevel_allows_above(void) {
    Logger logger;
    logger.begin();
    logger.setGlobalMinLevel(LogLevel::ERROR);
    MockTransport t;
    logger.addTransport(&t);

    logger.error("A", "err");
    logger.fatal("A", "fat");

    TEST_ASSERT_EQUAL_UINT8(2, t.count);
}

// ─────────────────────────────────────────────────────────────────────────────
// Transport per-transport minLevel filter
// ─────────────────────────────────────────────────────────────────────────────

void test_logger_transport_minLevel_filters_independently(void) {
    Logger logger;
    logger.begin();

    MockTransport tAll;   // receives everything (default minLevel = DEBUG)
    MockTransport tErr;   // receives only ERROR+
    tErr.setMinLevel(LogLevel::ERROR);

    logger.addTransport(&tAll);
    logger.addTransport(&tErr);

    logger.info("A", "info msg");
    logger.error("A", "error msg");

    TEST_ASSERT_EQUAL_UINT8(2, tAll.count);  // INFO + ERROR
    TEST_ASSERT_EQUAL_UINT8(1, tErr.count);  // only ERROR
    TEST_ASSERT_EQUAL(LogLevel::ERROR, tErr.entries[0].level);
}

// ─────────────────────────────────────────────────────────────────────────────
// message truncation at format buffer (256 chars)
// ─────────────────────────────────────────────────────────────────────────────

void test_logger_truncates_long_message(void) {
    Logger logger;
    logger.begin();
    MockTransport t;
    logger.addTransport(&t);

    // Build a 300-char format string (exceeds LOGGER_FORMAT_BUFFER_SIZE=256)
    char big[301];
    memset(big, 'X', 300);
    big[300] = '\0';

    logger.info("A", "%s", big);

    TEST_ASSERT_EQUAL_UINT8(1, t.count);
    // message field is 192 bytes — stored message must be <= 191 chars
    TEST_ASSERT_TRUE(strlen(t.entries[0].message) <= 191);
}

// ─────────────────────────────────────────────────────────────────────────────
// removeTransport
// ─────────────────────────────────────────────────────────────────────────────

void test_logger_removeTransport_stops_dispatch(void) {
    Logger logger;
    logger.begin();
    MockTransport t;
    logger.addTransport(&t);
    logger.removeTransport(&t);

    TEST_ASSERT_EQUAL_UINT8(0, logger.getTransportCount());
    logger.info("A", "msg");
    TEST_ASSERT_EQUAL_UINT8(0, t.count);  // nothing received after removal
}

// ─────────────────────────────────────────────────────────────────────────────
// Integration: Logger → RingBuffer → getLastError
// (mirrors the unit test example from LOGGER_ARCHITECTURE.md §11)
// ─────────────────────────────────────────────────────────────────────────────

void test_logger_ring_integration_getLastError(void) {
    Logger logger;
    logger.begin();
    RingBufferTransport ring(20, false);
    ring.begin();
    logger.addTransport(&ring);

    logger.info("System", "started");
    logger.info("LDC1101", "init ok");
    logger.error("LDC1101", "CHIP_ID mismatch: expected 0x04 got 0x00");
    logger.info("System", "retrying");

    LogEntry last;
    TEST_ASSERT_TRUE(ring.getLastError(last));
    TEST_ASSERT_EQUAL_STRING("LDC1101", last.component);
    TEST_ASSERT_NOT_NULL(strstr(last.message, "CHIP_ID"));
}

// ─────────────────────────────────────────────────────────────────────────────
// Runner
// ─────────────────────────────────────────────────────────────────────────────

void setUp(void)    { g_mock_millis = 0; }
void tearDown(void) {}

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_logger_begin_required_before_use);
    RUN_TEST(test_logger_addTransport_calls_begin);
    RUN_TEST(test_logger_addTransport_max_four);
    RUN_TEST(test_logger_addTransport_null_returns_false);

    RUN_TEST(test_logger_dispatches_to_transport);
    RUN_TEST(test_logger_dispatches_to_all_transports);
    RUN_TEST(test_logger_timestamp_comes_from_millis);

    RUN_TEST(test_logger_globalMinLevel_blocks_below);
    RUN_TEST(test_logger_globalMinLevel_allows_above);

    RUN_TEST(test_logger_transport_minLevel_filters_independently);

    RUN_TEST(test_logger_truncates_long_message);

    RUN_TEST(test_logger_removeTransport_stops_dispatch);

    RUN_TEST(test_logger_ring_integration_getLastError);

    return UNITY_END();
}
