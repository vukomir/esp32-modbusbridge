#include <unity.h>
#include <Arduino.h>
#include "LogStore.h"

// Counts every record currently readable, and optionally returns the first and
// last rendered lines.
static int drain(String *first = nullptr, String *last = nullptr)
{
    char buf[256];
    size_t n = 0;
    int count = 0;
    LogStore::Cursor c = LogStore::openRead();
    while (LogStore::next(c, buf, sizeof(buf), n))
    {
        if (count == 0 && first != nullptr)
        {
            *first = String(buf);
        }
        if (last != nullptr)
        {
            *last = String(buf);
        }
        count++;
    }
    return count;
}

void setUp(void)
{
    LogStore::_resetForTest();
    LogStore::begin();
}

void tearDown(void) {}

// A cold region (bad magic) must initialise cleanly rather than trusting
// whatever bytes happened to be in RTC RAM.
void test_cold_boot_initialises_clean()
{
    TEST_ASSERT_EQUAL_UINT32(1, LogStore::bootCount());
    TEST_ASSERT_EQUAL_UINT32(0, LogStore::droppedRecords());
    TEST_ASSERT_EQUAL_UINT16(LOG_STORE_DATA_BYTES, LogStore::capacityBytes());

    // begin() always leaves exactly the boot marker behind.
    String first;
    TEST_ASSERT_EQUAL_INT(1, drain(&first));
    TEST_ASSERT_TRUE(first.indexOf("=== BOOT #1") >= 0);
    TEST_ASSERT_TRUE(first.indexOf("reason=") >= 0);
}

// A valid region must survive: this is the entire point of the feature.
void test_warm_boot_preserves_previous_session()
{
    LogStore::sink(ESPLogger::ERROR, "previous session died here", 500);
    TEST_ASSERT_EQUAL_INT(2, drain());

    // Simulate a reboot: region stays intact, begin() runs again.
    LogStore::begin();

    String first, last;
    int count = drain(&first, &last);
    TEST_ASSERT_EQUAL_INT(3, count);
    TEST_ASSERT_EQUAL_UINT32(2, LogStore::bootCount());
    TEST_ASSERT_TRUE(first.indexOf("=== BOOT #1") >= 0);
    TEST_ASSERT_TRUE(last.indexOf("=== BOOT #2") >= 0);

    // The pre-reboot line is still in the middle of the file.
    char buf[256];
    size_t n = 0;
    bool found = false;
    LogStore::Cursor c = LogStore::openRead();
    while (LogStore::next(c, buf, sizeof(buf), n))
    {
        if (String(buf).indexOf("previous session died here") >= 0)
        {
            found = true;
        }
    }
    TEST_ASSERT_TRUE(found);
}

// Range invariants matter as much as the magic - a plausible magic with a
// nonsense writePos must not be trusted.
void test_corrupt_region_is_wiped()
{
    LogStore::sink(ESPLogger::ERROR, "should not survive", 500);
    LogStore::_corruptForTest();
    LogStore::begin();

    TEST_ASSERT_EQUAL_UINT32(1, LogStore::bootCount());
    String first;
    TEST_ASSERT_EQUAL_INT(1, drain(&first));
    TEST_ASSERT_TRUE(first.indexOf("=== BOOT #1") >= 0);
}

// Rendered format must be exactly "[<ts>][<LEVEL>] <message>\n" - the download
// is consumed by humans and by grep, so this is a contract.
void test_render_format()
{
    LogStore::_resetForTest();
    LogStore::begin();
    drain(); // consume marker position, content checked below

    LogStore::sink(ESPLogger::WARN, "hello world", 1234);

    char buf[256];
    size_t n = 0;
    size_t lastWritten = 0;
    String lastLine;
    LogStore::Cursor c = LogStore::openRead();
    while (LogStore::next(c, buf, sizeof(buf), n))
    {
        lastLine = String(buf);
        lastWritten = n; // next() zeroes `n` on the final, failing call
    }
    TEST_ASSERT_EQUAL_STRING("[1234][WARN] hello world\n", lastLine.c_str());
    // written must be the byte count actually produced, so the HTTP layer can
    // pass it straight to sendContent() without calling strlen again.
    TEST_ASSERT_EQUAL_UINT32(25, (uint32_t)lastWritten);
}

// Oldest records are evicted, dropped is counted, and the ring stays walkable
// across the wrap. This is the case most likely to corrupt the structure.
void test_wraparound_evicts_and_counts()
{
    char msg[80];
    // Far more than the ring can hold, forcing many wraps.
    for (int i = 0; i < 400; i++)
    {
        snprintf(msg, sizeof(msg), "line %04d padding padding padding padding padding", i);
        LogStore::sink(ESPLogger::ERROR, msg, (unsigned long)(i + 1));
    }

    TEST_ASSERT_TRUE(LogStore::droppedRecords() > 0);
    TEST_ASSERT_TRUE(LogStore::usedBytes() <= LogStore::capacityBytes());

    // Everything still readable, and the newest line must be the last one in.
    String first, last;
    int count = drain(&first, &last);
    TEST_ASSERT_TRUE(count > 0);
    TEST_ASSERT_TRUE(last.indexOf("line 0399") >= 0);
    // The boot marker is long gone.
    TEST_ASSERT_TRUE(first.indexOf("=== BOOT") < 0);
}

// Over-long messages are truncated rather than overflowing the record.
void test_long_message_truncated_safely()
{
    char big[LOG_STORE_MAX_MSG + 120];
    memset(big, 'x', sizeof(big) - 1);
    big[sizeof(big) - 1] = '\0';

    LogStore::sink(ESPLogger::ERROR, big, 42);

    char buf[512];
    size_t n = 0;
    String lastLine;
    LogStore::Cursor c = LogStore::openRead();
    while (LogStore::next(c, buf, sizeof(buf), n))
    {
        lastLine = String(buf);
    }
    // "[42][ERROR] " + LOG_STORE_MAX_MSG chars + "\n"
    TEST_ASSERT_EQUAL_UINT32((uint32_t)(strlen("[42][ERROR] ") + LOG_STORE_MAX_MSG + 1),
                             (uint32_t)lastLine.length());
    TEST_ASSERT_TRUE(LogStore::usedBytes() <= LogStore::capacityBytes());
}

// Boot window: everything is kept early, only WARN/ERROR later.
void test_capture_policy_boot_window()
{
    TEST_ASSERT_TRUE(LogStore::shouldCapture(ESPLogger::DEBUG, 30000));
    TEST_ASSERT_TRUE(LogStore::shouldCapture(ESPLogger::INFO, 30000));

    TEST_ASSERT_FALSE(LogStore::shouldCapture(ESPLogger::DEBUG, 90000));
    TEST_ASSERT_FALSE(LogStore::shouldCapture(ESPLogger::INFO, 90000));

    // Problems are always kept, whenever they happen.
    TEST_ASSERT_TRUE(LogStore::shouldCapture(ESPLogger::WARN, 90000));
    TEST_ASSERT_TRUE(LogStore::shouldCapture(ESPLogger::ERROR, 90000));
}

// An ERROR re-opens full capture, and the re-arm is rate limited so an error
// storm cannot hold the window open and flush the boot log out of the ring.
void test_error_reopens_window_and_rate_limits()
{
    // Outside the boot window, INFO is dropped...
    TEST_ASSERT_FALSE(LogStore::shouldCapture(ESPLogger::INFO, 100000));

    // ...until an ERROR re-arms it.
    LogStore::sink(ESPLogger::ERROR, "something broke", 100000);
    TEST_ASSERT_TRUE(LogStore::shouldCapture(ESPLogger::INFO, 100001));
    TEST_ASSERT_TRUE(LogStore::shouldCapture(ESPLogger::INFO,
                                             100000 + LOG_ERROR_WINDOW_MS - 1));
    // Window closes again.
    TEST_ASSERT_FALSE(LogStore::shouldCapture(ESPLogger::INFO,
                                              100000 + LOG_ERROR_WINDOW_MS));

    // A second error too soon must NOT extend the window.
    LogStore::sink(ESPLogger::ERROR, "and again", 100000 + LOG_ERROR_WINDOW_MS + 5);
    TEST_ASSERT_FALSE(LogStore::shouldCapture(ESPLogger::INFO,
                                              100000 + LOG_ERROR_WINDOW_MS + 6));

    // Once the gap has elapsed, it re-arms.
    unsigned long later = 100000 + LOG_ERROR_REARM_MIN_GAP_MS + 1;
    LogStore::sink(ESPLogger::ERROR, "much later", later);
    TEST_ASSERT_TRUE(LogStore::shouldCapture(ESPLogger::INFO, later + 1));
}

// A null message must not reach memcpy.
void test_null_message_ignored()
{
    int before = drain();
    LogStore::sink(ESPLogger::ERROR, nullptr, 10);
    TEST_ASSERT_EQUAL_INT(before, drain());
}

// An undersized output buffer must fail cleanly rather than write past the end.
void test_next_rejects_tiny_buffer()
{
    char tiny[1];
    size_t n = 0;
    LogStore::Cursor c = LogStore::openRead();
    TEST_ASSERT_FALSE(LogStore::next(c, tiny, sizeof(tiny), n));
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)n);
}

void setup()
{
    delay(2000);

    UNITY_BEGIN();

    RUN_TEST(test_cold_boot_initialises_clean);
    RUN_TEST(test_warm_boot_preserves_previous_session);
    RUN_TEST(test_corrupt_region_is_wiped);
    RUN_TEST(test_render_format);
    RUN_TEST(test_wraparound_evicts_and_counts);
    RUN_TEST(test_long_message_truncated_safely);
    RUN_TEST(test_capture_policy_boot_window);
    RUN_TEST(test_error_reopens_window_and_rate_limits);
    RUN_TEST(test_null_message_ignored);
    RUN_TEST(test_next_rejects_tiny_buffer);

    UNITY_END();
}

void loop()
{
    // Empty - tests run once in setup()
}
