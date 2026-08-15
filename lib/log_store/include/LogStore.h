#ifndef LOG_STORE_H
#define LOG_STORE_H

#include <Arduino.h>
#include <ESPLogger.h>
#include "constants.h"

// LogStore - log storage in RTC slow RAM, so a boot narrative survives into the
// next boot and can be read back after the fact.
//
// WHY RTC RAM: it survives ESP.restart(), a panic, and both watchdogs, at zero
// flash cost and zero flash wear. It does NOT survive a power cut, a brownout,
// or an RTC-watchdog reset - and the RTC watchdog is what fires when the
// *bootloader* hangs, so pre-app failures stay invisible. Don't oversell it.
//
// TWO SEGMENTS, NOT ONE RING. Field data settled this: a single ring was filled
// end to end by the known RS485 CRC noise (an ERROR plus a WARN every ~25s) and
// the startup narrative was evicted within ~16 minutes, which defeats the whole
// point. So:
//
//   boot segment - records from the first LOG_BOOT_WINDOW_MS of THIS boot.
//                  Linear, not circular: it fills once and then stops, so the
//                  earliest lines - the ones that diagnose a boot hang - can
//                  never be pushed out by anything later. Reset on every boot,
//                  so it always describes the current session. Once full,
//                  further boot-window records overflow into the rolling
//                  segment rather than being dropped.
//   rolling segment - everything else, evicting oldest. This is where the
//                  previous session's final moments live after a reboot.
//
// The two are read as separate sections, not spliced into one timeline: the
// rolling segment can hold records older than the boot segment, so pretending
// they form a single chronology would be a lie.
//
// LEVEL FILTERING, IMPORTANT: sinks are invoked from ESPLogger::print(), which
// runs *after* the global level gate. This store can therefore only ever see
// lines that already passed ESPLogger::getLevel(). With log_level set to "warn"
// the boot window captures no INFO at all - observed in the field, where it
// swallowed the entire WiFi and MQTT init narrative. Set log_level to "info"
// before you rely on this.
//
// THREAD SAFETY: sink() is called from ~420 sites across five FreeRTOS tasks on
// two physical cores. Guarded by a spinlock, not a mutex: a mutex can block, and
// CLAUDE.md forbids blocking inside MQTT/poller callbacks. The lock covers only
// the header write and the memcpy - never Serial, never the network.
class LogStore
{
public:
    static const uint8_t FLAG_BOOT = 0x01;      // synthetic boot marker
    static const uint8_t FLAG_TRUNCATED = 0x02; // longer than LOG_STORE_MAX_MSG
    static const uint8_t FLAG_WRAP = 0x04;      // tail filler; reader jumps to 0

    static const uint8_t SEG_BOOT = 0;
    static const uint8_t SEG_ROLLING = 1;
    static const uint8_t SEG_DONE = 2;

    // Validates (or reinitialises) the RTC region, clears the boot segment and
    // appends the boot marker. Call as early in setup() as possible -
    // everything logged before it is lost, which is how the original boot hang
    // went unrecorded.
    static void begin();

    // ESPLogger::LogCallback. Register with ESPLogger::addLogCallback().
    static void sink(ESPLogger::LogLevel level, const char *message, unsigned long timestamp);

    // Capture policy. Pure, and takes nowMs as a parameter rather than calling
    // millis(), because the native mocks derive millis() from a steady_clock
    // that tests cannot advance - passing the time in is the only way to test
    // the boot window at all.
    static bool shouldCapture(ESPLogger::LogLevel level, unsigned long nowMs);

    struct Cursor
    {
        uint8_t segment;       // segment being walked next (SEG_*)
        uint8_t recordSegment; // segment the record just returned came from
        uint16_t pos;
        uint16_t remaining;
        uint32_t seqAtStart;
        bool overrun; // writer lapped us mid-read
    };

    static Cursor openRead();

    // Renders one record as "[<ts>][<LEVEL>] <message>\n" into a caller-supplied
    // buffer. Returns false when exhausted. No String, no heap, and no knowledge
    // of HTTP - the transport lives in lib/webui.
    static bool next(Cursor &cursor, char *out, size_t outSize, size_t &written);

    static uint16_t usedBytes();
    static uint16_t capacityBytes();
    static uint16_t bootUsedBytes();
    static uint16_t bootCapacityBytes();
    static uint16_t rollingUsedBytes();
    static uint16_t rollingCapacityBytes();
    static uint32_t droppedRecords();
    static uint32_t bootCount();
    static const char *resetReasonStr();

#ifdef UNIT_TEST
    static void _resetForTest();   // force a cold-boot region
    static void _corruptForTest(); // valid magic, out-of-range field
#endif
};

#endif // LOG_STORE_H
