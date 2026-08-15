#include "LogStore.h"
#include <string.h>

// Injected by the dev/prod build flags; the native test env does not define
// them and does not need real values.
#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "unknown"
#endif
#ifndef GIT_HASH
#define GIT_HASH "unknown"
#endif

// --- Platform shims -------------------------------------------------------
// The native test build has no RTC memory and no FreeRTOS. The region becomes a
// plain static array and the spinlock becomes nothing, which is correct because
// the tests are single-threaded.
#ifdef NATIVE_BUILD
#define LS_RTC_ATTR
#define LS_LOCK_DECL(name) int name = 0
#define LS_LOCK_ENTER(name) ((void)0)
#define LS_LOCK_EXIT(name) ((void)0)
static uint32_t ls_reset_reason() { return 1; }
#else
#include <esp_system.h>
#define LS_RTC_ATTR RTC_NOINIT_ATTR
#define LS_LOCK_DECL(name) portMUX_TYPE name = portMUX_INITIALIZER_UNLOCKED
#define LS_LOCK_ENTER(name) portENTER_CRITICAL_SAFE(&name)
#define LS_LOCK_EXIT(name) portEXIT_CRITICAL_SAFE(&name)
static uint32_t ls_reset_reason() { return (uint32_t)esp_reset_reason(); }
#endif

namespace
{

// Bumped when the on-RTC layout changes; a mismatch wipes the region. The
// two-segment split is layout change 2.
const uint32_t LS_MAGIC = 0x4C4753BB;
const uint16_t LS_VERSION = 2;

const uint16_t DATA = LOG_STORE_DATA_BYTES;
const uint16_t BOOT_CAP = LOG_STORE_BOOT_BYTES;
const uint16_t ROLL_CAP = LOG_STORE_DATA_BYTES - LOG_STORE_BOOT_BYTES;
const uint16_t HDR = 8; // bytes of per-record header

struct Ring
{
    uint16_t base;      // offset of this segment within the data area
    uint16_t cap;       // bytes available to this segment
    uint16_t writePos;  // relative to base
    uint16_t readStart; // relative to base
    uint16_t used;
};

// Region header. Validated after a reboot, so it must not depend on anything
// else having survived.
struct Region
{
    uint32_t magic;
    uint16_t version;
    uint16_t pad;
    Ring boot;
    Ring roll;
    uint32_t seq;
    uint32_t dropped;
    uint32_t bootCount;
    uint32_t resetReason;
};

// Both live in RTC slow RAM and are deliberately NOT zero-initialised - that is
// the whole point of RTC_NOINIT_ATTR. Validity is decided by magic plus range
// checks in begin(), never by assuming a clean state.
//
// No CRC, deliberately. Checksumming 7KB on every write to defend against a
// 1-in-2^32 cold-boot false positive whose worst outcome is some garbage text
// in a file a human reads with their eyes is not a good trade. Do not "fix".
LS_RTC_ATTR Region g_region;
LS_RTC_ATTR uint8_t g_data[DATA];

LS_LOCK_DECL(g_mux);

inline uint16_t roundUp4(uint16_t n) { return (uint16_t)((n + 3) & ~((uint16_t)3)); }

// Header access goes through memcpy so no field is ever reached by a misaligned
// 32-bit load on the RTC bus.
inline void readHdr(uint16_t off, uint16_t &len, uint8_t &level, uint8_t &flags, uint32_t &ts)
{
    memcpy(&len, &g_data[off], sizeof(len));
    level = g_data[off + 2];
    flags = g_data[off + 3];
    memcpy(&ts, &g_data[off + 4], sizeof(ts));
}

inline void writeHdr(uint16_t off, uint16_t len, uint8_t level, uint8_t flags, uint32_t ts)
{
    memcpy(&g_data[off], &len, sizeof(len));
    g_data[off + 2] = level;
    g_data[off + 3] = flags;
    memcpy(&g_data[off + 4], &ts, sizeof(ts));
}

// Size of the record starting at `pos` (relative to the ring base).
uint16_t recordSizeAt(const Ring &r, uint16_t pos)
{
    if (pos > r.cap - HDR)
    {
        return (uint16_t)(r.cap - pos); // too little tail left for a header
    }

    uint16_t len;
    uint8_t level, flags;
    uint32_t ts;
    readHdr((uint16_t)(r.base + pos), len, level, flags, ts);

    if (flags & LogStore::FLAG_WRAP)
    {
        return (uint16_t)(r.cap - pos);
    }
    return (uint16_t)(HDR + roundUp4(len));
}

void resetRing(Ring &r, uint16_t base, uint16_t cap)
{
    r.base = base;
    r.cap = cap;
    r.writePos = 0;
    r.readStart = 0;
    r.used = 0;
}

// Drop whole records from the front of a circular ring until `need` fits.
void evict(Ring &r, uint16_t need, uint32_t &dropped)
{
    while (r.used + need > r.cap && r.used > 0)
    {
        uint16_t sz = recordSizeAt(r, r.readStart);
        if (sz == 0 || sz > r.used)
        {
            // Structure is inconsistent - salvage by resetting this segment
            // rather than looping forever on a corrupt length.
            resetRing(r, r.base, r.cap);
            return;
        }
        r.readStart = (uint16_t)((r.readStart + sz) % r.cap);
        r.used = (uint16_t)(r.used - sz);
        dropped++;
    }
}

// Append into a circular, evicting ring.
void appendCircular(Ring &r, uint8_t level, uint8_t flags, uint32_t ts,
                    const char *msg, uint16_t len, uint32_t &dropped)
{
    const uint16_t need = (uint16_t)(HDR + roundUp4(len));

    // Records never straddle the end, so every header stays contiguous and
    // 4-byte aligned. If the tail cannot hold this record, the remainder
    // becomes filler and we restart at 0.
    uint16_t tailGap = (uint16_t)(r.cap - r.writePos);
    if (tailGap < need)
    {
        evict(r, tailGap, dropped);
        if (tailGap >= HDR)
        {
            writeHdr((uint16_t)(r.base + r.writePos), 0, 0, LogStore::FLAG_WRAP, 0);
        }
        r.used = (uint16_t)(r.used + tailGap);
        r.writePos = 0;
    }

    evict(r, need, dropped);

    const uint16_t off = (uint16_t)(r.base + r.writePos);
    writeHdr(off, len, level, flags, ts);
    if (len > 0)
    {
        memcpy(&g_data[off + HDR], msg, len);
    }
    for (uint16_t i = (uint16_t)(HDR + len); i < need; i++)
    {
        g_data[off + i] = 0; // zero padding so stale bytes never render
    }

    r.writePos = (uint16_t)((r.writePos + need) % r.cap);
    r.used = (uint16_t)(r.used + need);
}

// Append into the linear, fill-once boot segment. Returns false when it is
// full, so the caller can overflow into the rolling segment instead of losing
// the record. Never evicts: the earliest lines of a boot are the ones that
// diagnose a boot hang, so nothing later is allowed to displace them.
bool appendBoot(Ring &r, uint8_t level, uint8_t flags, uint32_t ts,
                const char *msg, uint16_t len)
{
    const uint16_t need = (uint16_t)(HDR + roundUp4(len));
    if (r.writePos + need > r.cap)
    {
        return false;
    }

    const uint16_t off = (uint16_t)(r.base + r.writePos);
    writeHdr(off, len, level, flags, ts);
    if (len > 0)
    {
        memcpy(&g_data[off + HDR], msg, len);
    }
    for (uint16_t i = (uint16_t)(HDR + len); i < need; i++)
    {
        g_data[off + i] = 0;
    }

    r.writePos = (uint16_t)(r.writePos + need);
    r.used = r.writePos;
    return true;
}

const char *levelName(uint8_t level)
{
    switch (level)
    {
    case ESPLogger::ERROR:
        return "ERROR";
    case ESPLogger::WARN:
        return "WARN";
    case ESPLogger::INFO:
        return "INFO";
    case ESPLogger::DEBUG:
        return "DEBUG";
    default:
        return "UNKNOWN";
    }
}

const char *resetReasonName(uint32_t reason)
{
    // Mirrors esp_reset_reason_t. A switch on the raw value so the native build
    // can exercise it without the ESP headers.
    switch (reason)
    {
    case 1:
        return "POWERON";
    case 2:
        return "EXT";
    case 3:
        return "SW_RESET";
    case 4:
        return "PANIC";
    case 5:
        return "INT_WDT";
    case 6:
        return "TASK_WDT";
    case 7:
        return "OTHER_WDT";
    case 8:
        return "DEEPSLEEP";
    case 9:
        return "BROWNOUT";
    case 10:
        return "SDIO";
    default:
        return "UNKNOWN";
    }
}

} // namespace

const uint8_t LogStore::FLAG_BOOT;
const uint8_t LogStore::FLAG_TRUNCATED;
const uint8_t LogStore::FLAG_WRAP;
const uint8_t LogStore::SEG_BOOT;
const uint8_t LogStore::SEG_ROLLING;
const uint8_t LogStore::SEG_DONE;

static unsigned long s_captureUntilMs = 0;
static unsigned long s_lastRearmMs = 0;

void LogStore::begin()
{
    const bool valid =
        g_region.magic == LS_MAGIC &&
        g_region.version == LS_VERSION &&
        g_region.boot.base == 0 && g_region.boot.cap == BOOT_CAP &&
        g_region.roll.base == BOOT_CAP && g_region.roll.cap == ROLL_CAP &&
        g_region.boot.writePos <= BOOT_CAP && g_region.boot.used <= BOOT_CAP &&
        g_region.roll.writePos < ROLL_CAP && g_region.roll.readStart < ROLL_CAP &&
        g_region.roll.used <= ROLL_CAP;

    if (!valid)
    {
        g_region.magic = LS_MAGIC;
        g_region.version = LS_VERSION;
        g_region.pad = 0;
        g_region.seq = 0;
        g_region.dropped = 0;
        g_region.bootCount = 1;
        resetRing(g_region.roll, BOOT_CAP, ROLL_CAP);
    }
    else
    {
        g_region.bootCount++;
    }
    g_region.resetReason = ls_reset_reason();

    // The boot segment always describes the CURRENT session. The previous
    // session's final moments survive in the rolling segment, which is the half
    // that actually matters after a crash.
    resetRing(g_region.boot, 0, BOOT_CAP);

    s_captureUntilMs = 0;
    s_lastRearmMs = 0;

    // The single highest-value line in the file: it says whether the previous
    // session died or was simply unplugged.
    char marker[LOG_STORE_MAX_MSG];
    snprintf(marker, sizeof(marker),
             "=== BOOT #%lu reason=%s fw=%s git=%s ===",
             (unsigned long)g_region.bootCount,
             resetReasonName(g_region.resetReason),
             FIRMWARE_VERSION,
             GIT_HASH);
    sink(ESPLogger::INFO, marker, 0);
}

bool LogStore::shouldCapture(ESPLogger::LogLevel level, unsigned long nowMs)
{
    if (level <= ESPLogger::WARN) // ERROR=0, WARN=1
    {
        return true;
    }
    if (nowMs < LOG_BOOT_WINDOW_MS)
    {
        return true;
    }
    return nowMs < s_captureUntilMs;
}

void LogStore::sink(ESPLogger::LogLevel level, const char *message, unsigned long timestamp)
{
    if (message == nullptr)
    {
        return;
    }

    if (shouldCapture(level, timestamp))
    {
        uint16_t len = (uint16_t)strlen(message);
        uint8_t flags = 0;
        if (len > LOG_STORE_MAX_MSG)
        {
            len = LOG_STORE_MAX_MSG;
            flags |= FLAG_TRUNCATED;
        }

        LS_LOCK_ENTER(g_mux);
        // Boot-window records go to the protected segment; once it is full they
        // overflow into the rolling one rather than being lost.
        bool stored = false;
        if (timestamp < LOG_BOOT_WINDOW_MS)
        {
            stored = appendBoot(g_region.boot, (uint8_t)level, flags,
                                (uint32_t)timestamp, message, len);
        }
        if (!stored)
        {
            appendCircular(g_region.roll, (uint8_t)level, flags,
                           (uint32_t)timestamp, message, len, g_region.dropped);
        }
        g_region.seq++;
        LS_LOCK_EXIT(g_mux);
    }

    // An ERROR re-opens full capture, so the aftermath is recorded and not just
    // the failure line. Rate-limited so a CRC storm cannot hold the window open
    // forever and flush useful history out of the rolling segment.
    if (level == ESPLogger::ERROR && timestamp - s_lastRearmMs >= LOG_ERROR_REARM_MIN_GAP_MS)
    {
        s_lastRearmMs = timestamp;
        s_captureUntilMs = timestamp + LOG_ERROR_WINDOW_MS;
    }
}

LogStore::Cursor LogStore::openRead()
{
    Cursor c;
    LS_LOCK_ENTER(g_mux);
    c.segment = SEG_BOOT;
    c.recordSegment = SEG_BOOT;
    c.pos = g_region.boot.readStart;
    c.remaining = g_region.boot.used;
    c.seqAtStart = g_region.seq;
    LS_LOCK_EXIT(g_mux);
    c.overrun = false;
    return c;
}

bool LogStore::next(Cursor &cursor, char *out, size_t outSize, size_t &written)
{
    written = 0;
    if (out == nullptr || outSize < 2)
    {
        return false;
    }

    uint16_t len = 0;
    uint8_t level = 0, flags = 0;
    uint32_t ts = 0;
    char body[LOG_STORE_MAX_MSG + 1];
    bool haveRecord = false;
    uint8_t fromSegment = SEG_DONE;

    // One short critical section per record: copy it out, then release before
    // any formatting. Never hold the lock across a network write.
    LS_LOCK_ENTER(g_mux);

    while (cursor.segment != SEG_DONE && !haveRecord)
    {
        Ring &r = (cursor.segment == SEG_BOOT) ? g_region.boot : g_region.roll;

        if (cursor.remaining == 0)
        {
            // Move to the next segment.
            if (cursor.segment == SEG_BOOT)
            {
                cursor.segment = SEG_ROLLING;
                cursor.pos = g_region.roll.readStart;
                cursor.remaining = g_region.roll.used;
            }
            else
            {
                cursor.segment = SEG_DONE;
            }
            continue;
        }

        // The writer lapped us: everything from here is younger than where we
        // started, so the read is no longer coherent.
        if (r.used < cursor.remaining)
        {
            cursor.overrun = true;
            cursor.remaining = 0;
            continue;
        }

        uint16_t pos = cursor.pos;
        uint16_t sz;

        if (pos > r.cap - HDR)
        {
            sz = (uint16_t)(r.cap - pos);
            cursor.remaining = (cursor.remaining > sz) ? (uint16_t)(cursor.remaining - sz) : 0;
            cursor.pos = 0;
            continue;
        }

        readHdr((uint16_t)(r.base + pos), len, level, flags, ts);

        if (flags & FLAG_WRAP)
        {
            sz = (uint16_t)(r.cap - pos);
            cursor.remaining = (cursor.remaining > sz) ? (uint16_t)(cursor.remaining - sz) : 0;
            cursor.pos = 0;
            continue;
        }

        if (len > LOG_STORE_MAX_MSG)
        {
            cursor.remaining = 0; // corrupt length - stop rather than overrun
            continue;
        }

        sz = (uint16_t)(HDR + roundUp4(len));
        if (len > 0)
        {
            memcpy(body, &g_data[r.base + pos + HDR], len);
        }
        body[len] = '\0';

        cursor.pos = (uint16_t)((pos + sz) % r.cap);
        cursor.remaining = (cursor.remaining > sz) ? (uint16_t)(cursor.remaining - sz) : 0;
        fromSegment = cursor.segment;
        haveRecord = true;
    }

    LS_LOCK_EXIT(g_mux);

    if (!haveRecord)
    {
        return false;
    }

    cursor.recordSegment = fromSegment;

    int n = snprintf(out, outSize, "[%lu][%s] %s\n",
                     (unsigned long)ts, levelName(level), body);
    if (n < 0)
    {
        return false;
    }
    written = ((size_t)n < outSize) ? (size_t)n : (outSize - 1);
    return true;
}

uint16_t LogStore::usedBytes() { return (uint16_t)(g_region.boot.used + g_region.roll.used); }
uint16_t LogStore::capacityBytes() { return DATA; }
uint16_t LogStore::bootUsedBytes() { return g_region.boot.used; }
uint16_t LogStore::bootCapacityBytes() { return BOOT_CAP; }
uint16_t LogStore::rollingUsedBytes() { return g_region.roll.used; }
uint16_t LogStore::rollingCapacityBytes() { return ROLL_CAP; }
uint32_t LogStore::droppedRecords() { return g_region.dropped; }
uint32_t LogStore::bootCount() { return g_region.bootCount; }
const char *LogStore::resetReasonStr() { return resetReasonName(g_region.resetReason); }

#ifdef UNIT_TEST
void LogStore::_resetForTest()
{
    g_region.magic = 0;
    s_captureUntilMs = 0;
    s_lastRearmMs = 0;
}

void LogStore::_corruptForTest()
{
    g_region.magic = LS_MAGIC;
    g_region.version = LS_VERSION;
    g_region.roll.writePos = (uint16_t)(ROLL_CAP + 1); // out of range
}
#endif
