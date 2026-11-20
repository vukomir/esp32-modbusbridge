#pragma once

#include <Arduino.h>
#include <vector>

/**
 * Holds telemetry values that could not be published, so they can be sent as
 * soon as MQTT works again.
 *
 * Why this matters more with tiered polling: a live value that fails to
 * publish is replaced a second later anyway, but an energy counter is only
 * read every 30 seconds and a constant every few minutes. Without a resend, a
 * dropped publish - or a short MQTT outage - left such a value stale in Home
 * Assistant until its next scheduled read.
 *
 * One entry per metric name: remembering a name that is already held replaces
 * its value, so the buffer never grows beyond the number of distinct metrics
 * and only ever resends the newest value.
 *
 * Header-only and free of MQTT/Modbus includes so it can be unit-tested on the
 * host (test/test_telemetry_retry). It is used from the poller task only and
 * does no locking.
 */
class TelemetryRetryBuffer
{
public:
    struct Entry
    {
        String name;
        String unit;
        String value;
    };

    // The SolPlanet driver publishes ~110 distinct metrics; 160 leaves room
    // for custom devices up to their documented ~150 field ceiling.
    static constexpr size_t DEFAULT_CAPACITY = 160;

    explicit TelemetryRetryBuffer(size_t capacity = DEFAULT_CAPACITY) : cap(capacity) {}

    // Keep `value` for a later resend. Replaces any value already held for
    // `name`. If the buffer is full, the oldest entry is dropped to make room.
    void remember(const String &name, const String &unit, const String &value)
    {
        if (cap == 0)
        {
            return;
        }
        for (size_t i = 0; i < items.size(); i++)
        {
            if (items[i].name == name)
            {
                items[i].unit = unit;
                items[i].value = value;
                return;
            }
        }
        if (items.size() >= cap)
        {
            items.erase(items.begin());
        }
        Entry e;
        e.name = name;
        e.unit = unit;
        e.value = value;
        items.push_back(e);
    }

    // Drop the held value for `name`, if any - a newer one is about to be
    // published, so the old one must not be resent after it.
    void forget(const String &name)
    {
        for (size_t i = 0; i < items.size(); i++)
        {
            if (items[i].name == name)
            {
                items.erase(items.begin() + i);
                return;
            }
        }
    }

    // Try to send everything held, oldest first. `send(entry)` returns true on
    // success. Stops at the first failure: if one publish fails the connection
    // is most likely gone, and the rest would only wait for the same timeout.
    // Returns how many entries were sent (and removed).
    template <typename SendFn>
    size_t flush(SendFn send)
    {
        size_t sent = 0;
        while (sent < items.size())
        {
            if (!send(items[sent]))
            {
                break;
            }
            sent++;
        }
        if (sent > 0)
        {
            items.erase(items.begin(), items.begin() + sent);
        }
        return sent;
    }

    size_t size() const { return items.size(); }
    bool empty() const { return items.empty(); }
    size_t capacity() const { return cap; }
    void clear() { items.clear(); }

    // Value currently held for `name`, or nullptr.
    const Entry *find(const String &name) const
    {
        for (size_t i = 0; i < items.size(); i++)
        {
            if (items[i].name == name)
            {
                return &items[i];
            }
        }
        return nullptr;
    }

private:
    size_t cap;
    std::vector<Entry> items;
};
