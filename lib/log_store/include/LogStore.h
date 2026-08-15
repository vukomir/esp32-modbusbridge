#ifndef LOG_STORE_H
#define LOG_STORE_H

#include <Arduino.h>
#include <ESPLogger.h>
#include "constants.h"

// LogStore - a log ring buffer in RTC slow RAM, so a boot narrative survives
// into the next boot and can be read back after the fact.
//
// WHY RTC RAM: it survives ESP.restart(), a panic, and both watchdogs, at zero
// flash cost and zero flash wear. It does NOT survive a power cut, a brownout,
// or an RTC-watchdog reset - and the RTC watchdog is what fires when the
// *bootloader* hangs, so pre-app failures stay invisible. Don't oversell it.
//
// LEVEL FILTERING, IMPORTANT: sinks are invoked from ESPLogger::print(), which
// runs *after* the global level gate. This store can therefore only ever see
// lines that already passed ESPLogger::getLevel(). shouldCapture() returning
// true for DEBUG during the boot window means nothing unless log_level is
// already "debug" (the default is "info"). That is deliberate - a second level
// concept would be more state for something the user can solve by raising the
// level before reproducing.
//
// THREAD SAFETY: sink() is called from ~420 sites across five FreeRTOS tasks on
// two physical cores. The ring is guarded by a spinlock, not a mutex: a mutex
// can block, and CLAUDE.md forbids blocking inside MQTT/poller callbacks. The
// lock covers only the header write and the memcpy - never Serial, never the
// network.
class LogStore
{
public:
    // Record flags, stored in the per-record header.
    static const uint8_t FLAG_BOOT = 0x01;     // synthetic boot marker
    static const uint8_t FLAG_TRUNCATED = 0x02; // message was longer than LOG_STORE_MAX_MSG
    static const uint8_t FLAG_WRAP = 0x04;     // filler at the tail; reader jumps to 0

    // Validates (or reinitialises) the RTC region and appends the boot marker.
    // Call this as early in setup() as possible - everything logged before it
    // is lost, which is exactly how the original boot bug went unrecorded.
    static void begin();

    // ESPLogger::LogCallback. Registered via ESPLogger::addLogCallback().
    static void sink(ESPLogger::LogLevel level, const char *message, unsigned long timestamp);

    // Capture policy. Pure and takes nowMs as a parameter rather than calling
    // millis(), because the native mocks derive millis() from a steady_clock
    // that tests cannot advance - passing the time in is the only way to test
    // the boot window at all.
    static bool shouldCapture(ESPLogger::LogLevel level, unsigned long nowMs);

    // --- Read side ---
    struct Cursor
    {
        uint16_t pos;       // byte offset into the ring
        uint16_t remaining; // bytes left to walk
        uint32_t seqAtStart;
        bool overrun;       // writer lapped us mid-read
    };

    static Cursor openRead();

    // Renders one record as "[<ts>][<LEVEL>] <message>\n" into a caller-supplied
    // buffer. Returns false when the cursor is exhausted. No String, no heap,
    // and no knowledge of HTTP - the transport lives in lib/webui.
    static bool next(Cursor &cursor, char *out, size_t outSize, size_t &written);

    static uint16_t usedBytes();
    static uint16_t capacityBytes();
    static uint32_t droppedRecords();
    static uint32_t bootCount();
    static const char *resetReasonStr();

#ifdef UNIT_TEST
    // Forces the region back to a cold-boot state so tests can exercise the
    // validity paths deterministically.
    static void _resetForTest();
    // Pretends the region survived a reboot with a corrupt field.
    static void _corruptForTest();
#endif

private:
    static void reinit();
    static void appendRecord(uint8_t level, uint8_t flags, uint32_t ts,
                             const char *msg, uint16_t len);
    static uint16_t recordSizeAt(uint16_t pos);
    static void evict(uint16_t need);

    static unsigned long captureUntilMs;
    static unsigned long lastRearmMs;
};

#endif // LOG_STORE_H
