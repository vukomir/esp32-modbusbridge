#include <unity.h>
#include <Arduino.h>
#include "ESPLogger.h"

// Test variables
String capturedLog;
bool logCallbackCalled;

// Mock log callback for testing
void testLogCallback(ESPLogger::LogLevel level, const char *message, unsigned long timestamp)
{
    const char *levelStr;
    switch (level)
    {
    case ESPLogger::DEBUG:
        levelStr = "DEBUG";
        break;
    case ESPLogger::INFO:
        levelStr = "INFO";
        break;
    case ESPLogger::WARN:
        levelStr = "WARN";
        break;
    case ESPLogger::ERROR:
        levelStr = "ERROR";
        break;
    default:
        levelStr = "UNKNOWN";
        break;
    }
    capturedLog = String(levelStr) + ": " + String(message);
    logCallbackCalled = true;
}

// Second sink, used only by the multi-sink tests below.
String secondCapturedLog;
bool secondCallbackCalled;

void secondLogCallback(ESPLogger::LogLevel level, const char *message, unsigned long timestamp)
{
    (void)level;
    (void)timestamp;
    secondCapturedLog = String(message);
    secondCallbackCalled = true;
}

void setUp(void)
{
    capturedLog = "";
    logCallbackCalled = false;
    secondCapturedLog = "";
    secondCallbackCalled = false;
    ESPLogger::setLogCallback(testLogCallback);
}

void tearDown(void)
{
    ESPLogger::setLogCallback(nullptr);
    ESPLogger::setLevel(ESPLogger::INFO); // Reset to default
}

// Test log level setting and getting
void test_log_level_operations()
{
    ESPLogger::setLevel(ESPLogger::DEBUG);
    TEST_ASSERT_EQUAL_INT(ESPLogger::DEBUG, ESPLogger::getLevel());

    ESPLogger::setLevel(ESPLogger::ERROR);
    TEST_ASSERT_EQUAL_INT(ESPLogger::ERROR, ESPLogger::getLevel());

    ESPLogger::setLevel(ESPLogger::WARN);
    TEST_ASSERT_EQUAL_INT(ESPLogger::WARN, ESPLogger::getLevel());
}

// Test basic logging functionality
void test_basic_logging()
{
    ESPLogger::setLevel(ESPLogger::INFO);

    ESPLogger::info("Test info message");
    TEST_ASSERT_TRUE(logCallbackCalled);
    TEST_ASSERT_TRUE(capturedLog.indexOf("INFO") >= 0);
    TEST_ASSERT_TRUE(capturedLog.indexOf("Test info message") >= 0);
}

// Test log level filtering
void test_log_level_filtering()
{
    // Test that debug messages are filtered when level is INFO
    setUp(); // Reset
    ESPLogger::setLevel(ESPLogger::INFO);

    ESPLogger::debug("Debug message should be filtered");
    TEST_ASSERT_FALSE(logCallbackCalled);

    // Test that info messages pass through
    ESPLogger::info("Info message should pass");
    TEST_ASSERT_TRUE(logCallbackCalled);
}

// Test different log levels
void test_different_log_levels()
{
    ESPLogger::setLevel(ESPLogger::DEBUG);

    ESPLogger::debug("Debug message");
    TEST_ASSERT_TRUE(logCallbackCalled);
    TEST_ASSERT_TRUE(capturedLog.indexOf("DEBUG") >= 0);

    setUp(); // Reset
    ESPLogger::warn("Warning message");
    TEST_ASSERT_TRUE(logCallbackCalled);
    TEST_ASSERT_TRUE(capturedLog.indexOf("WARN") >= 0);

    setUp(); // Reset
    ESPLogger::error("Error message");
    TEST_ASSERT_TRUE(logCallbackCalled);
    TEST_ASSERT_TRUE(capturedLog.indexOf("ERROR") >= 0);
}

// Test formatted logging
void test_formatted_logging()
{
    ESPLogger::setLevel(ESPLogger::INFO);

    ESPLogger::info("Test %s with number %d", "formatted", 42);
    TEST_ASSERT_TRUE(logCallbackCalled);
    TEST_ASSERT_TRUE(capturedLog.indexOf("formatted") >= 0);
    TEST_ASSERT_TRUE(capturedLog.indexOf("42") >= 0);
}

// Test callback functionality
void test_callback_functionality()
{
    // Test without callback
    ESPLogger::setLogCallback(nullptr);
    ESPLogger::info("No callback test");
    TEST_ASSERT_FALSE(logCallbackCalled);

    // Test with callback
    ESPLogger::setLogCallback(testLogCallback);
    ESPLogger::info("With callback test");
    TEST_ASSERT_TRUE(logCallbackCalled);
}

// Test message content
void test_message_content()
{
    ESPLogger::setLevel(ESPLogger::INFO);

    String testMessage = "Test message content";
    ESPLogger::info(testMessage.c_str());

    TEST_ASSERT_TRUE(logCallbackCalled);
    TEST_ASSERT_TRUE(capturedLog.indexOf(testMessage) >= 0);
}

// Both registered sinks must receive every line. This is the property the
// WebUI console and LogStore depend on to coexist.
void test_multiple_sinks_both_fire()
{
    ESPLogger::setLevel(ESPLogger::INFO);
    TEST_ASSERT_TRUE(ESPLogger::addLogCallback(secondLogCallback));

    ESPLogger::info("Broadcast to both");

    TEST_ASSERT_TRUE(logCallbackCalled);
    TEST_ASSERT_TRUE(secondCallbackCalled);
    TEST_ASSERT_TRUE(capturedLog.indexOf("Broadcast to both") >= 0);
    TEST_ASSERT_TRUE(secondCapturedLog.indexOf("Broadcast to both") >= 0);

    // Slots are finite and duplicates are rejected rather than silently
    // consuming a slot.
    TEST_ASSERT_FALSE(ESPLogger::addLogCallback(secondLogCallback));
    TEST_ASSERT_FALSE(ESPLogger::addLogCallback(nullptr));
}

// Removing one sink by identity must leave the other registered — the failure
// mode this whole change exists to prevent.
void test_remove_single_sink_leaves_other()
{
    ESPLogger::setLevel(ESPLogger::INFO);
    TEST_ASSERT_TRUE(ESPLogger::addLogCallback(secondLogCallback));

    TEST_ASSERT_TRUE(ESPLogger::removeLogCallback(secondLogCallback));
    TEST_ASSERT_FALSE(ESPLogger::removeLogCallback(secondLogCallback)); // already gone

    ESPLogger::info("Only the first sink");

    TEST_ASSERT_TRUE(logCallbackCalled);
    TEST_ASSERT_FALSE(secondCallbackCalled);
}

void setup()
{
    delay(2000); // Wait for serial monitor

    UNITY_BEGIN();

    RUN_TEST(test_log_level_operations);
    RUN_TEST(test_basic_logging);
    RUN_TEST(test_log_level_filtering);
    RUN_TEST(test_different_log_levels);
    RUN_TEST(test_formatted_logging);
    RUN_TEST(test_callback_functionality);
    RUN_TEST(test_message_content);
    RUN_TEST(test_multiple_sinks_both_fire);
    RUN_TEST(test_remove_single_sink_leaves_other);

    UNITY_END();
}

void loop()
{
    // Empty - tests run once in setup()
}
