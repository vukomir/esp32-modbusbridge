#include <unity.h>
#include <Arduino.h>
#include "TelemetryRetryBuffer.h"
#include <vector>
#include <string>

// Host-side suite for the buffer that keeps telemetry which failed to publish.
// Hardware-free, so it runs for real under `pio test -e native`.

void setUp(void) {}
void tearDown(void) {}

// Records what was "published" and can be told to start failing.
struct FakeBroker
{
    std::vector<std::string> sent; // "name=value"
    int failAfter = -1;            // -1 = never fail; otherwise fail once this many were sent

    bool publish(const TelemetryRetryBuffer::Entry &e)
    {
        if (failAfter >= 0 && (int)sent.size() >= failAfter)
        {
            return false;
        }
        sent.push_back(std::string(e.name.c_str()) + "=" + e.value.c_str());
        return true;
    }
};

static size_t flushTo(TelemetryRetryBuffer &buf, FakeBroker &broker)
{
    return buf.flush([&broker](const TelemetryRetryBuffer::Entry &e) { return broker.publish(e); });
}

void test_starts_empty()
{
    TelemetryRetryBuffer buf;
    TEST_ASSERT_TRUE(buf.empty());
    TEST_ASSERT_EQUAL_UINT(0, buf.size());
    FakeBroker broker;
    TEST_ASSERT_EQUAL_UINT(0, flushTo(buf, broker));
}

// The basic promise: what failed is sent later, in the order it failed.
void test_remembered_values_are_resent_in_order()
{
    TelemetryRetryBuffer buf;
    buf.remember("energy_today", "kWh", "12.3");
    buf.remember("grid_code", "", "89");
    buf.remember("battery_soc", "%", "81");

    FakeBroker broker;
    TEST_ASSERT_EQUAL_UINT(3, flushTo(buf, broker));
    TEST_ASSERT_TRUE(buf.empty());
    TEST_ASSERT_EQUAL_UINT(3, broker.sent.size());
    TEST_ASSERT_EQUAL_STRING("energy_today=12.3", broker.sent[0].c_str());
    TEST_ASSERT_EQUAL_STRING("grid_code=89", broker.sent[1].c_str());
    TEST_ASSERT_EQUAL_STRING("battery_soc=81", broker.sent[2].c_str());
}

// Only the newest value of a metric is worth resending.
void test_same_name_keeps_only_the_newest_value()
{
    TelemetryRetryBuffer buf;
    buf.remember("active_power", "W", "5000");
    buf.remember("energy_today", "kWh", "12.3");
    buf.remember("active_power", "W", "5775");

    TEST_ASSERT_EQUAL_UINT(2, buf.size());
    const TelemetryRetryBuffer::Entry *e = buf.find("active_power");
    TEST_ASSERT_NOT_NULL(e);
    TEST_ASSERT_EQUAL_STRING("5775", e->value.c_str());
    TEST_ASSERT_EQUAL_STRING("W", e->unit.c_str());
}

// A value superseded by a fresh reading must not be resent after it.
void test_forget_drops_a_superseded_value()
{
    TelemetryRetryBuffer buf;
    buf.remember("active_power", "W", "5000");
    buf.remember("energy_today", "kWh", "12.3");

    buf.forget("active_power");
    buf.forget("not_held"); // harmless

    FakeBroker broker;
    TEST_ASSERT_EQUAL_UINT(1, flushTo(buf, broker));
    TEST_ASSERT_EQUAL_STRING("energy_today=12.3", broker.sent[0].c_str());
}

// If the connection dies part-way through, nothing is lost and nothing is
// sent twice.
void test_flush_stops_at_first_failure_and_keeps_the_rest()
{
    TelemetryRetryBuffer buf;
    buf.remember("a", "", "1");
    buf.remember("b", "", "2");
    buf.remember("c", "", "3");
    buf.remember("d", "", "4");

    FakeBroker broker;
    broker.failAfter = 2;
    TEST_ASSERT_EQUAL_UINT(2, flushTo(buf, broker));
    TEST_ASSERT_EQUAL_UINT(2, buf.size());
    TEST_ASSERT_NULL(buf.find("a"));
    TEST_ASSERT_NULL(buf.find("b"));
    TEST_ASSERT_NOT_NULL(buf.find("c"));
    TEST_ASSERT_NOT_NULL(buf.find("d"));

    // Connection back: the remainder goes out, once.
    broker.failAfter = -1;
    TEST_ASSERT_EQUAL_UINT(2, flushTo(buf, broker));
    TEST_ASSERT_TRUE(buf.empty());
    TEST_ASSERT_EQUAL_UINT(4, broker.sent.size());
    TEST_ASSERT_EQUAL_STRING("c=3", broker.sent[2].c_str());
    TEST_ASSERT_EQUAL_STRING("d=4", broker.sent[3].c_str());
}

// A long outage cannot grow the buffer without bound.
void test_capacity_drops_the_oldest_entry()
{
    TelemetryRetryBuffer buf(3);
    buf.remember("a", "", "1");
    buf.remember("b", "", "2");
    buf.remember("c", "", "3");
    buf.remember("d", "", "4");

    TEST_ASSERT_EQUAL_UINT(3, buf.size());
    TEST_ASSERT_NULL(buf.find("a"));
    TEST_ASSERT_NOT_NULL(buf.find("d"));

    // Updating a held name when full does not evict anything.
    buf.remember("b", "", "20");
    TEST_ASSERT_EQUAL_UINT(3, buf.size());
    TEST_ASSERT_NOT_NULL(buf.find("c"));
    TEST_ASSERT_EQUAL_STRING("20", buf.find("b")->value.c_str());
}

// The same metrics failing every second for an hour is still one entry each.
void test_repeated_failures_do_not_grow_the_buffer()
{
    TelemetryRetryBuffer buf;
    for (int cycle = 0; cycle < 3600; cycle++)
    {
        buf.remember("active_power", "W", String(cycle));
        buf.remember("l1_voltage", "V", String(cycle));
    }
    TEST_ASSERT_EQUAL_UINT(2, buf.size());
    TEST_ASSERT_EQUAL_STRING("3599", buf.find("active_power")->value.c_str());
}

void test_zero_capacity_holds_nothing()
{
    TelemetryRetryBuffer buf(0);
    buf.remember("a", "", "1");
    TEST_ASSERT_TRUE(buf.empty());
}

void test_clear()
{
    TelemetryRetryBuffer buf;
    buf.remember("a", "", "1");
    buf.clear();
    TEST_ASSERT_TRUE(buf.empty());
}

void setup()
{
    UNITY_BEGIN();

    RUN_TEST(test_starts_empty);
    RUN_TEST(test_remembered_values_are_resent_in_order);
    RUN_TEST(test_same_name_keeps_only_the_newest_value);
    RUN_TEST(test_forget_drops_a_superseded_value);
    RUN_TEST(test_flush_stops_at_first_failure_and_keeps_the_rest);
    RUN_TEST(test_capacity_drops_the_oldest_entry);
    RUN_TEST(test_repeated_failures_do_not_grow_the_buffer);
    RUN_TEST(test_zero_capacity_holds_nothing);
    RUN_TEST(test_clear);

    UNITY_END();
}

void loop()
{
    // Empty - tests run once in setup()
}
