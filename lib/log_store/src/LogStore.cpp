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
// The native test build has no RTC memory and no FreeRTOS. The ring becomes a
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

// Bumped only when the on-RTC layout changes; a mismatch wipes the region.
const uint32_t LS_MAGIC = 0x4C4753BB;
const uint16_t LS_VERSION = 1;

const uint16_t CAP = LOG_STORE_DATA_BYTES;
const uint16_t HDR = 8; // bytes of per-record header

// Region header. Kept to 32 bytes and 4-byte aligned; it is the first thing
// validated after a reboot, so it must not depend on anything else surviving.
struct Region
{
    uint32_t magic;
    uint16_t version;
    uint16_t writePos;
    uint16_t readStart;
    uint16_t used;
    uint32_t seq;
    uint32_t dropped;
    uint32_t bootCount;
    uint32_t resetReason;
    uint32_t reserved;
};

// Both live in RTC slow RAM and are deliberately NOT zero-initialised - that is
// the whole point of RTC_NOINIT_ATTR. Validity is decided by magic + range
// checks in reinit(), never by assuming a clean state.
//
// No CRC, deliberately. Checksumming 7KB on every write to defend against a
// 1-in-2^32 cold-boot false positive whose worst outcome is some garbage text
// in a file a human reads with their eyes is not a good trade. Do not "fix".
LS_RTC_ATTR Region g_region;
LS_RTC_ATTR uint8_t g_data[CAP];

LS_LOCK_DECL(g_mux);

inline uint16_t roundUp4(uint16_t n) { return (uint16_t)((n + 3) & ~((uint16_t)3)); }

// Header access goes through memcpy so no field is ever reached by a
// misaligned 32-bit load on the RTC bus.
inline void readHdr(uint16_t pos, uint16_t &len, uint8_t &level, uint8_t &flags, uint32_t &ts)
{
    memcpy(&len, &g_data[pos], sizeof(len));
    level = g_data[pos + 2];
    flags = g_data[pos + 3];
    memcpy(&ts, &g_data[pos + 4], sizeof(ts));
}

inline void writeHdr(uint16_t pos, uint16_t len, uint8_t level, uint8_t flags, uint32_t ts)
{
    memcpy(&g_data[pos], &len, sizeof(len));
    g_data[pos + 2] = level;
    g_data[pos + 3] = flags;
    memcpy(&g_data[pos + 4], &ts, sizeof(ts));
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
    // Mirrors esp_reset_reason_t. Kept as a switch on the raw value so the
    // native build can exercise it without the ESP headers.
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

unsigned long LogStore::captureUntilMs = 0;
unsigned long LogStore::lastRearmMs = 0;

const uint8_t LogStore::FLAG_BOOT;
const uint8_t LogStore::FLAG_TRUNCATED;
const uint8_t LogStore::FLAG_WRAP;

void LogStore::reinit()
{
    g_region.magic = LS_MAGIC;
    g_region.version = LS_VERSION;
    g_region.writePos = 0;
    g_region.readStart = 0;
    g_region.used = 0;
    g_region.seq = 0;
    g_region.dropped = 0;
    g_region.bootCount = 1;
    g_region.resetReason = ls_reset_reason();
    g_region.reserved = 0;
}

uint16_t LogStore::recordSizeAt(uint16_t pos)
{
    // Too little tail left to hold a header: the remainder is implicit filler.
    if (pos > CAP - HDR)
    {
        return (uint16_t)(CAP - pos);
    }

    uint16_t len;
    uint8_t level, flags;
    uint32_t ts;
    readHdr(pos, len, level, flags, ts);

    if (flags & FLAG_WRAP)
    {
        return (uint16_t)(CAP - pos);
    }
    return (uint16_t)(HDR + roundUp4(len));
}

void LogStore::evict(uint16_t need)
{
    // Drop whole records from the front until the request fits. Bounded by the
    // ring being non-empty; `used` shrinks by at least HDR each iteration.
    while (g_region.used + need > CAP && g_region.used > 0)
    {
        uint16_t sz = recordSizeAt(g_region.readStart);
        if (sz == 0 || sz > g_region.used)
        {
            // Structure is inconsistent - salvage by resetting rather than
            // looping forever on a corrupt length.
            reinit();
            return;
        }
        g_region.readStart = (uint16_t)((g_region.readStart + sz) % CAP);
        g_region.used = (uint16_t)(g_region.used - sz);
        g_region.dropped++;
    }
}

void LogStore::appendRecord(uint8_t level, uint8_t flags, uint32_t ts,
                            const char *msg, uint16_t len)
{
    if (len > LOG_STORE_MAX_MSG)
    {
        len = LOG_STORE_MAX_MSG;
        flags |= FLAG_TRUNCATED;
    }

    const uint16_t need = (uint16_t)(HDR + roundUp4(len));

    LS_LOCK_ENTER(g_mux);

    // Records never straddle the end of the ring, so every header stays
    // contiguous and 4-byte aligned. If the tail cannot hold this record, the
    // remainder becomes filler and we restart at 0.
    uint16_t tailGap = (uint16_t)(CAP - g_region.writePos);
    if (tailGap < need)
    {
        evict(tailGap);
        if (tailGap >= HDR)
        {
            writeHdr(g_region.writePos, 0, 0, FLAG_WRAP, 0);
        }
        g_region.used = (uint16_t)(g_region.used + tailGap);
        g_region.writePos = 0;
    }

    evict(need);

    writeHdr(g_region.writePos, len, level, flags, ts);
    if (len > 0)
    {
        memcpy(&g_data[g_region.writePos + HDR], msg, len);
    }
    // Zero the padding so stale bytes never leak into a rendered line.
    for (uint16_t i = (uint16_t)(HDR + len); i < need; i++)
    {
        g_data[g_region.writePos + i] = 0;
    }

    g_region.writePos = (uint16_t)((g_region.writePos + need) % CAP);
    g_region.used = (uint16_t)(g_region.used + need);
    g_region.seq++;

    LS_LOCK_EXIT(g_mux);
}

void LogStore::begin()
{
    const bool valid =
        g_region.magic == LS_MAGIC &&
        g_region.version == LS_VERSION &&
        g_region.writePos < CAP &&
        g_region.readStart < CAP &&
        g_region.used <= CAP;

    if (!valid)
    {
        reinit();
    }
    else
    {
        g_region.bootCount++;
        g_region.resetReason = ls_reset_reason();
    }

    captureUntilMs = 0;
    lastRearmMs = 0;

    // The single highest-value line in the file: it says whether the previous
    // session died or was simply unplugged.
    char marker[LOG_STORE_MAX_MSG];
    snprintf(marker, sizeof(marker),
             "=== BOOT #%lu reason=%s fw=%s git=%s ===",
             (unsigned long)g_region.bootCount,
             resetReasonName(g_region.resetReason),
             FIRMWARE_VERSION,
             GIT_HASH);
    appendRecord((uint8_t)ESPLogger::INFO, FLAG_BOOT, 0, marker, (uint16_t)strlen(marker));
}

bool LogStore::shouldCapture(ESPLogger::LogLevel level, unsigned long nowMs)
{
    // ERROR=0, WARN=1 - always worth keeping.
    if (level <= ESPLogger::WARN)
    {
        return true;
    }
    if (nowMs < LOG_BOOT_WINDOW_MS)
    {
        return true;
    }
    return nowMs < captureUntilMs;
}

void LogStore::sink(ESPLogger::LogLevel level, const char *message, unsigned long timestamp)
{
    if (message == nullptr)
    {
        return;
    }

    if (shouldCapture(level, timestamp))
    {
        appendRecord((uint8_t)level, 0, (uint32_t)timestamp, message, (uint16_t)strlen(message));
    }

    // An ERROR re-opens full capture, so the aftermath is recorded and not just
    // the failure line. Rate-limited so a Modbus CRC storm cannot hold the
    // window open forever and flush the boot narrative out of the ring.
    if (level == ESPLogger::ERROR && timestamp - lastRearmMs >= LOG_ERROR_REARM_MIN_GAP_MS)
    {
        lastRearmMs = timestamp;
        captureUntilMs = timestamp + LOG_ERROR_WINDOW_MS;
    }
}

LogStore::Cursor LogStore::openRead()
{
    Cursor c;
    LS_LOCK_ENTER(g_mux);
    c.pos = g_region.readStart;
    c.remaining = g_region.used;
    c.seqAtStart = g_region.seq;
    LS_LOCK_EXIT(g_mux);
    c.overrun = false;
    return c;
}

bool LogStore::next(Cursor &cursor, char *out, size_t outSize, size_t &written)
{
    written = 0;
    if (out == nullptr || outSize < 2 || cursor.remaining == 0)
    {
        return false;
    }

    uint16_t len;
    uint8_t level, flags;
    uint32_t ts;
    char body[LOG_STORE_MAX_MSG + 1];
    uint16_t sz;

    // One short critical section per record: copy it out, then release before
    // doing any formatting. Never hold the lock across a network write.
    LS_LOCK_ENTER(g_mux);

    // The writer lapped us: everything from here is younger than where we
    // started, so the read is no longer coherent.
    if (g_region.used < cursor.remaining)
    {
        cursor.overrun = true;
        cursor.remaining = 0;
        LS_LOCK_EXIT(g_mux);
        return false;
    }

    uint16_t pos = cursor.pos;
    if (pos > CAP - HDR)
    {
        sz = (uint16_t)(CAP - pos);
        pos = 0;
        cursor.remaining = (cursor.remaining > sz) ? (uint16_t)(cursor.remaining - sz) : 0;
        if (cursor.remaining == 0)
        {
            LS_LOCK_EXIT(g_mux);
            return false;
        }
    }

    readHdr(pos, len, level, flags, ts);

    if (flags & FLAG_WRAP)
    {
        sz = (uint16_t)(CAP - pos);
        cursor.remaining = (cursor.remaining > sz) ? (uint16_t)(cursor.remaining - sz) : 0;
        pos = 0;
        if (cursor.remaining == 0)
        {
            cursor.pos = 0;
            LS_LOCK_EXIT(g_mux);
            return false;
        }
        readHdr(pos, len, level, flags, ts);
    }

    if (len > LOG_STORE_MAX_MSG)
    {
        // Corrupt length - stop rather than read out of bounds.
        cursor.remaining = 0;
        LS_LOCK_EXIT(g_mux);
        return false;
    }

    sz = (uint16_t)(HDR + roundUp4(len));
    if (len > 0)
    {
        memcpy(body, &g_data[pos + HDR], len);
    }
    body[len] = '\0';

    cursor.pos = (uint16_t)((pos + sz) % CAP);
    cursor.remaining = (cursor.remaining > sz) ? (uint16_t)(cursor.remaining - sz) : 0;

    LS_LOCK_EXIT(g_mux);

    int n = snprintf(out, outSize, "[%lu][%s] %s\n",
                     (unsigned long)ts, levelName(level), body);
    if (n < 0)
    {
        return false;
    }
    written = ((size_t)n < outSize) ? (size_t)n : (outSize - 1);
    return true;
}

uint16_t LogStore::usedBytes() { return g_region.used; }
uint16_t LogStore::capacityBytes() { return CAP; }
uint32_t LogStore::droppedRecords() { return g_region.dropped; }
uint32_t LogStore::bootCount() { return g_region.bootCount; }
const char *LogStore::resetReasonStr() { return resetReasonName(g_region.resetReason); }

#ifdef UNIT_TEST
void LogStore::_resetForTest()
{
    g_region.magic = 0;
    captureUntilMs = 0;
    lastRearmMs = 0;
}

void LogStore::_corruptForTest()
{
    g_region.magic = LS_MAGIC;
    g_region.version = LS_VERSION;
    g_region.writePos = CAP + 1; // out of range
}
#endif
