#include "SolplanetASW.h"
#include "ModbusClient.h"
#include "constants.h"
#include <ESPLogger.h>

// Store large constant strings in PROGMEM to save RAM

// Helper macro to create TelemetryPoint with full metadata
#define TP_FULL(name, unit, value, reg, type, scale) \
    TelemetryPoint(name, unit, value, reg, type, scale)

SolplanetASW::SolplanetASW(ModbusClient &modbus, Config &config)
    : modbus(modbus), config(config), slaveAddr(1), isThreePhase(false), phaseDetected(false),
      mpptCount(0), stringCount(0), configDetected(false),
      baseIntervalMs(10000), mediumIntervalMs(10000), slowIntervalMs(30000),
      coalesce(true), maxSpan(MAX_SPAN), longFrameStrikes(0)
{
    slaveAddr = config.getInt("rtu_addr", 1);

    // Tier periods. Read once: like the poll interval itself, a change takes
    // effect on the next restart.
    int baseSec = config.getInt("poll_interval_sec", 10);
    int mediumSec = config.getInt("poll_medium_interval_sec", 10);
    int slowSec = config.getInt("poll_slow_interval_sec", 30);
    if (baseSec < 1) baseSec = 1;
    if (mediumSec < 1) mediumSec = 1;
    if (slowSec < 1) slowSec = 1;
    baseIntervalMs = (uint32_t)baseSec * 1000u;
    mediumIntervalMs = (uint32_t)mediumSec * 1000u;
    slowIntervalMs = (uint32_t)slowSec * 1000u;

    // ---- Register map ------------------------------------------------------
    // MUST stay sorted by register address: neighbouring blocks that are due in
    // the same cycle are merged into one transaction, and the merge only looks
    // at the next entry.
    //
    // Constants: read at boot, then every 10x the slow interval.
    addBlock(31002, 1, TIER_STATIC, 0, &SolplanetASW::parseModbusAddress);
    addBlock(31027, 1, TIER_STATIC, 0, &SolplanetASW::parseGridCode);
    addBlock(31301, 2, TIER_STATIC, 0, &SolplanetASW::parseRatedValues);
    // AC energy today/total and operating hours. 0.1 kWh resolution, so they
    // change at most every few seconds even at full power.
    addBlock(31303, 6, TIER_SLOW, 0, &SolplanetASW::parseEnergyCounters);
    // Device state, connect time, temperatures, bus voltage.
    addBlock(31309, 9, TIER_MEDIUM, 0, &SolplanetASW::parseDeviceStatus);
    // Live PV and grid values. 31319-31379 is one contiguous span, so on a
    // normal cycle these three blocks are a single 61-register read.
    addBlock(31319, 20, TIER_FAST, BLOCK_REQUIRED, &SolplanetASW::parsePVInputs);
    addBlock(31339, 20, TIER_FAST, BLOCK_STRINGS, &SolplanetASW::parseStringCurrents);
    addBlock(31359, 21, TIER_FAST, BLOCK_REQUIRED, &SolplanetASW::parseGridAndPhase);
    addBlock(31391, 1, TIER_MEDIUM, 0, &SolplanetASW::parseGridConnection);
    // PV total power. The PV-side energy counters share the block and come
    // along for the cost of four extra registers.
    addBlock(31601, 6, TIER_FAST, 0, &SolplanetASW::parsePVTotals);
    addBlock(31607, 23, TIER_MEDIUM, BLOCK_STORAGE, &SolplanetASW::parseBattery);
    addBlock(31630, 4, TIER_SLOW, 0, &SolplanetASW::parseAcEnergy);
    // EPS (backup output) load and per-phase grid power: 31634-31674 is one
    // contiguous 41-register read on a 3-phase hybrid.
    addBlock(31634, 3, TIER_FAST, 0, &SolplanetASW::parseEpsBasic);
    addBlock(31637, 8, TIER_FAST, 0, &SolplanetASW::parseEpsPower);
    addBlock(31645, 18, TIER_FAST, BLOCK_THREE_PHASE, &SolplanetASW::parseEpsPhases);
    addBlock(31663, 12, TIER_FAST, BLOCK_THREE_PHASE, &SolplanetASW::parseGridPhasePower);
    addBlock(31675, 4, TIER_SLOW, 0, &SolplanetASW::parseGridExport);
    addBlock(31679, 3, TIER_SLOWER, BLOCK_STORAGE, &SolplanetASW::parseBmsDiagnostics);
    addBlock(31685, 3, TIER_SLOW, BLOCK_STORAGE, &SolplanetASW::parseGenerator);
}

void SolplanetASW::addBlock(uint16_t reg, uint8_t count, Tier tier, uint8_t flags, BlockParser parse)
{
    Block b;
    b.reg = reg;
    b.count = count;
    b.tier = (uint8_t)tier;
    b.flags = flags;
    b.parse = parse;
    b.nextDueMs = 0;
    b.scheduled = false;
    b.everRead = false;
    b.unsupported = false;
    b.failStreak = 0;
    blocks.push_back(b);
}

bool SolplanetASW::begin()
{
    ESPLogger::info("Initializing SolPlanet ASW inverter (GEN/HYBRID), slave address: %d", slaveAddr);

    if (!modbus.isInitialized())
    {
        ESPLogger::error("Modbus client not initialized");
        return false;
    }

    ESPLogger::info("Polling tiers: fast %lus, medium %lus, slow %lus",
                    (unsigned long)(baseIntervalMs / 1000), (unsigned long)(mediumIntervalMs / 1000),
                    (unsigned long)(slowIntervalMs / 1000));

    // Detect phase configuration
    if (!detectPhaseConfiguration())
    {
        ESPLogger::warn("Failed to detect phase configuration, will retry on first read");
    }

    return true;
}

bool SolplanetASW::detectPhaseConfiguration()
{
    if (phaseDetected)
    {
        return true;
    }

    // Auto-detect phase configuration by reading Device Type register (31001)
    // According to MB001_ASW GEN-Modbus-en_V2.1.7.pdf page 5:
    // Register 31001: Device Type String
    // Returns: ASCII '1' (49) = Single phase, ASCII '3' (51) = Three phase

    ESPLogger::info("Auto-detecting phase configuration from register 31001...");

    uint16_t deviceType;
    if (modbus.readInputRegisters(slaveAddr, inputRegisterToModbus(31001), 1, &deviceType))
    {
        // Check ASCII value (the register stores '1' or '3' as ASCII)
        if (deviceType == 49 || deviceType == '1')  // ASCII '1' = single phase
        {
            isThreePhase = false;
            ESPLogger::info("✓ Detected 1-phase inverter (device type: %u / '%c')", deviceType, (char)deviceType);
        }
        else if (deviceType == 51 || deviceType == '3')  // ASCII '3' = three phase
        {
            isThreePhase = true;
            ESPLogger::info("✓ Detected 3-phase inverter (device type: %u / '%c')", deviceType, (char)deviceType);
        }
        else
        {
            // Unknown value - log warning but DON'T latch detection as successful
            ESPLogger::warn("Unknown device type value: %u (0x%04X), cannot determine phase configuration", deviceType, deviceType);
            return false;
        }

        phaseDetected = true;
        return true;
    }
    else
    {
        ESPLogger::error("Failed to read device type register 31001");
        ESPLogger::error("Check: RS485 wiring, slave address (%d), baud rate (9600)", slaveAddr);
        return false;
    }
}

bool SolplanetASW::detectDeviceConfiguration()
{
    if (configDetected)
    {
        return true;
    }

    ESPLogger::info("Detecting device configuration (MPPT & string counts)...");

    bool success = true;

    // Read register 31074: MPPT number (U16)
    // This tells us how many PV/MPPT inputs the inverter has (typically 2-4)
    uint16_t mpptNum;
    if (modbus.readInputRegisters(slaveAddr, inputRegisterToModbus(31074), 1, &mpptNum))
    {
        mpptCount = mpptNum;
        ESPLogger::info("✓ Detected %u MPPT/PV inputs", mpptCount);

        // Validate reasonable range (ASW models have 2-4 MPPT typically)
        if (mpptCount == 0 || mpptCount > 10)
        {
            ESPLogger::warn("Unusual MPPT count: %u, defaulting to 2", mpptCount);
            mpptCount = 2;
        }
    }
    else
    {
        ESPLogger::warn("Failed to read MPPT count (register 31074), defaulting to 2");
        mpptCount = 2;
        success = false;
    }

    // Read register 31075: String current number (U16)
    // This tells us how many individual string currents to read (registers 31339-31358)
    uint16_t stringNum;
    if (modbus.readInputRegisters(slaveAddr, inputRegisterToModbus(31075), 1, &stringNum))
    {
        stringCount = stringNum;
        ESPLogger::info("✓ Detected %u individual strings", stringCount);

        // Validate reasonable range (up to 20 strings per PDF)
        if (stringCount > 20)
        {
            ESPLogger::warn("String count exceeds 20: %u, capping at 20", stringCount);
            stringCount = 20;
        }
    }
    else
    {
        ESPLogger::info("No individual string currents detected (register 31075)");
        stringCount = 0;
        // Not a failure - some models don't have separate string currents
    }

    configDetected = true;
    return success;
}

uint16_t SolplanetASW::inputRegisterToModbus(uint16_t inputRegister) const
{
    // Convert 3xxxx input register number to 0-based Modbus address
    // Per MB001 doc page 3: "31001 (decimal) → 1000 (decimal) → 0x03e8"
    return inputRegister - 30001;
}

// ===========================================================================
// Tiered block scheduler
// ===========================================================================

uint32_t SolplanetASW::tierPeriodMs(uint8_t tier) const
{
    switch (tier)
    {
    case TIER_MEDIUM: return mediumIntervalMs;
    case TIER_SLOW:   return slowIntervalMs;
    case TIER_SLOWER: return slowIntervalMs * 2u;
    case TIER_STATIC: return slowIntervalMs * 10u;
    default:          return 0; // TIER_FAST: every cycle
    }
}

bool SolplanetASW::blockEnabled(const Block &b, bool storageEnabled) const
{
    if ((b.flags & BLOCK_THREE_PHASE) && !isThreePhase) return false;
    if ((b.flags & BLOCK_STRINGS) && stringCount == 0) return false;
    if ((b.flags & BLOCK_STORAGE) && !storageEnabled) return false;
    return true;
}

bool SolplanetASW::blockDue(const Block &b, uint32_t now) const
{
    if (!b.scheduled)
    {
        return true;
    }
    // Signed difference so the comparison survives millis() wrapping.
    return (int32_t)(now - b.nextDueMs) >= 0;
}

size_t SolplanetASW::runEnd(const bool *selected, size_t first) const
{
    size_t end = first + 1;
    // A parked block is probed on its own: merging it would take its healthy
    // neighbours down with it.
    if (!coalesce || blocks[first].unsupported)
    {
        return end;
    }

    uint16_t nextReg = blocks[first].reg + blocks[first].count;
    uint16_t total = blocks[first].count;
    while (end < blocks.size() && selected[end] && !blocks[end].unsupported &&
           blocks[end].reg == nextReg && total + blocks[end].count <= maxSpan)
    {
        nextReg += blocks[end].count;
        total += blocks[end].count;
        end++;
    }
    return end;
}

uint32_t SolplanetASW::estimateCycleMs(const bool *selected) const
{
    // Wire time is exact for the configured baud rate. The fixed part per
    // transaction (inverter turnaround plus transceiver switching) is an
    // allowance, not a measurement.
    static const uint32_t PER_TRANSACTION_OVERHEAD_MS = 40;

    uint32_t total = 0;
    size_t i = 0;
    while (i < blocks.size())
    {
        if (!selected[i])
        {
            i++;
            continue;
        }
        size_t end = runEnd(selected, i);
        uint16_t count = 0;
        for (size_t k = i; k < end; k++)
        {
            count += blocks[k].count;
        }
        total += PER_TRANSACTION_OVERHEAD_MS + modbus.frameTimeMs(8 + ModbusClient::responseLength(count));
        i = end;
    }
    return total;
}

void SolplanetASW::markRead(Block &b, uint32_t now)
{
    if (b.unsupported)
    {
        ESPLogger::info("Register block %u-%u is answering again", b.reg, b.reg + b.count - 1);
    }
    b.everRead = true;
    b.unsupported = false;
    b.failStreak = 0;
    b.scheduled = true;

    uint32_t period = tierPeriodMs(b.tier);
    // Due a little early rather than a whole cycle late: poll cycles are not
    // perfectly periodic, and a 10s block that lands at 9.99s should be read
    // on that cycle, not the one after.
    uint32_t tolerance = period / 10;
    if (tolerance > 200) tolerance = 200;
    b.nextDueMs = now + period - tolerance;
}

void SolplanetASW::markFailed(Block &b, uint32_t now, ModbusReadError error)
{
    if (b.failStreak < 255)
    {
        b.failStreak++;
    }
    b.scheduled = true;

    // The inverter said no (exception), or an optional block has now failed
    // several times running. Either way stop paying for it on every cycle.
    // Required blocks are never parked: if they fail, the poll fails and the
    // availability topic takes it from there.
    bool park = (error == ModbusReadError::Exception) ||
                (!(b.flags & BLOCK_REQUIRED) && b.failStreak >= UNSUPPORTED_AFTER_FAILURES);
    if (park && !(b.flags & BLOCK_REQUIRED))
    {
        if (!b.unsupported)
        {
            ESPLogger::info("Register block %u-%u not available on this inverter (%s); next probe in %lu min",
                            b.reg, b.reg + b.count - 1,
                            error == ModbusReadError::Exception ? "Modbus exception" : "repeated failures",
                            (unsigned long)(UNSUPPORTED_RETRY_MS / 60000));
        }
        b.unsupported = true;
        b.nextDueMs = now + UNSUPPORTED_RETRY_MS;
        return;
    }

    // Transient failure: fast blocks simply come round again next cycle; slower
    // ones are retried soon instead of waiting out their whole period.
    uint32_t retry = tierPeriodMs(b.tier);
    if (retry > 5000) retry = 5000;
    b.nextDueMs = now + retry;
}

bool SolplanetASW::readBlockAlone(Block &b, uint32_t now, std::vector<TelemetryPoint> &out)
{
    uint16_t buf[MAX_SPAN];
    // A block that is already failing gets one attempt, not three: if it is
    // genuinely absent the retries only delay everything behind it.
    uint8_t attempts = (b.failStreak > 0 || b.unsupported) ? 1 : ModbusClient::MAX_READ_ATTEMPTS;
    if (modbus.readInputRegisters(slaveAddr, inputRegisterToModbus(b.reg), b.count, buf, attempts))
    {
        (this->*b.parse)(buf, out);
        markRead(b, now);
        return true;
    }
    markFailed(b, now, modbus.getLastError());
    return false;
}

bool SolplanetASW::readBasic(std::vector<TelemetryPoint> &out)
{
    out.clear();

    // Ensure phase configuration is detected
    if (!phaseDetected && !detectPhaseConfiguration())
    {
        ESPLogger::error("Cannot read telemetry without phase detection");
        return false;
    }

    // Ensure device configuration is detected (MPPT count, etc.)
    if (!configDetected)
    {
        detectDeviceConfiguration(); // Non-fatal if it fails, uses defaults
    }

    const uint32_t now = millis();
    const bool storageEnabled = config.getBool("read_storage_regs", false);
    const size_t n = blocks.size() < MAX_BLOCKS ? blocks.size() : MAX_BLOCKS;

    // ---- 1. Which blocks are due? ------------------------------------------
    bool selected[MAX_BLOCKS] = {false};
    uint8_t deferrable[MAX_BLOCKS];
    size_t deferrableCount = 0;

    for (size_t i = 0; i < n; i++)
    {
        const Block &b = blocks[i];
        if (!blockEnabled(b, storageEnabled) || !blockDue(b, now))
        {
            continue;
        }
        // Fast blocks always go. So does anything not yet read once, so the
        // first cycle after boot publishes the complete picture.
        if (b.tier == TIER_FAST || (!b.everRead && !b.unsupported))
        {
            selected[i] = true;
        }
        else
        {
            deferrable[deferrableCount++] = (uint8_t)i;
        }
    }

    // ---- 2. Fit the slower blocks into what is left of the cycle ------------
    // Most overdue first. A block that does not fit stays due and is picked up
    // on a following cycle, which also spreads blocks sharing a period across
    // different cycles instead of bunching them on the same one.
    for (size_t a = 1; a < deferrableCount; a++)
    {
        uint8_t v = deferrable[a];
        int32_t overdue = (int32_t)(now - blocks[v].nextDueMs);
        size_t k = a;
        while (k > 0 && (int32_t)(now - blocks[deferrable[k - 1]].nextDueMs) < overdue)
        {
            deferrable[k] = deferrable[k - 1];
            k--;
        }
        deferrable[k] = v;
    }

    const uint32_t budgetMs = baseIntervalMs / 10u * 7u;
    bool slowTaken = false;
    size_t deferred = 0;
    for (size_t a = 0; a < deferrableCount; a++)
    {
        uint8_t i = deferrable[a];
        selected[i] = true;
        // The first one is always taken, so the slow tiers cannot be starved
        // even if the fast tier alone fills the cycle.
        if (slowTaken && estimateCycleMs(selected) > budgetMs)
        {
            selected[i] = false;
            deferred++;
        }
        else
        {
            slowTaken = true;
        }
    }

    // ---- 3. Read, merging neighbours into single transactions ---------------
    bool requiredFailed = false;
    bool aborted = false;
    size_t blocksOk = 0;
    size_t transactions = 0;
    uint16_t buf[MAX_SPAN];

    size_t i = 0;
    while (i < n)
    {
        if (!selected[i])
        {
            i++;
            continue;
        }
        size_t end = runEnd(selected, i);
        if (end > n) end = n;

        bool runRequired = false;
        uint16_t total = 0;
        for (size_t k = i; k < end; k++)
        {
            total += blocks[k].count;
            if (blocks[k].flags & BLOCK_REQUIRED) runRequired = true;
        }

        if (aborted)
        {
            // The device stopped answering earlier in this cycle. Do not sit
            // through a timeout for every remaining block as well.
            for (size_t k = i; k < end; k++)
            {
                markFailed(blocks[k], now, ModbusReadError::Timeout);
            }
            if (runRequired) requiredFailed = true;
            i = end;
            continue;
        }

        if (end - i == 1)
        {
            transactions++;
            if (readBlockAlone(blocks[i], now, out))
            {
                blocksOk++;
            }
            else
            {
                if (runRequired)
                {
                    requiredFailed = true;
                    if (modbus.getLastError() == ModbusReadError::Timeout) aborted = true;
                }
            }
            i = end;
            continue;
        }

        // Merged read of blocks i..end-1.
        transactions++;
        if (modbus.readInputRegisters(slaveAddr, inputRegisterToModbus(blocks[i].reg), total, buf))
        {
            uint16_t offset = 0;
            for (size_t k = i; k < end; k++)
            {
                (this->*blocks[k].parse)(buf + offset, out);
                offset += blocks[k].count;
                markRead(blocks[k], now);
                blocksOk++;
            }
            longFrameStrikes = 0;
            i = end;
            continue;
        }

        ModbusReadError mergedError = modbus.getLastError();
        if (mergedError == ModbusReadError::Timeout)
        {
            // Nothing came back at all. Splitting the read would only repeat
            // the same silence once per block.
            for (size_t k = i; k < end; k++)
            {
                markFailed(blocks[k], now, mergedError);
            }
            if (runRequired)
            {
                requiredFailed = true;
                aborted = true;
            }
            i = end;
            continue;
        }

        // Exception or corrupt frame on the merged read. Read the parts one by
        // one: that still delivers this cycle's data and shows which part, if
        // any, is the problem.
        bool allPartsOk = true;
        for (size_t k = i; k < end; k++)
        {
            if (aborted)
            {
                markFailed(blocks[k], now, ModbusReadError::Timeout);
                if (blocks[k].flags & BLOCK_REQUIRED) requiredFailed = true;
                allPartsOk = false;
                continue;
            }
            transactions++;
            if (readBlockAlone(blocks[k], now, out))
            {
                blocksOk++;
            }
            else
            {
                allPartsOk = false;
                if (blocks[k].flags & BLOCK_REQUIRED)
                {
                    requiredFailed = true;
                    if (modbus.getLastError() == ModbusReadError::Timeout) aborted = true;
                }
            }
        }

        if (allPartsOk)
        {
            if (mergedError == ModbusReadError::Exception)
            {
                // Every part is fine on its own but the inverter refuses them
                // together. Stop merging.
                coalesce = false;
                ESPLogger::warn("Inverter rejects merged read %u-%u but accepts its parts; "
                                "reading blocks individually from now on",
                                blocks[i].reg, blocks[end - 1].reg + blocks[end - 1].count - 1);
            }
            else if (++longFrameStrikes >= 3 && maxSpan > MIN_SPAN)
            {
                maxSpan = (maxSpan / 2 < MIN_SPAN) ? MIN_SPAN : maxSpan / 2;
                longFrameStrikes = 0;
                ESPLogger::warn("Long reads keep failing on the bus while short ones succeed; "
                                "limiting reads to %u registers", maxSpan);
            }
        }
        i = end;
    }

    ESPLogger::debug("Read %u telemetry points from %u block(s) in %u transaction(s), %u deferred (%s)",
                     (unsigned)out.size(), (unsigned)blocksOk, (unsigned)transactions,
                     (unsigned)deferred, isThreePhase ? "3-phase" : "1-phase");

    return !requiredFailed && blocksOk > 0;
}

bool SolplanetASW::readStorage(std::vector<TelemetryPoint> &out)
{
    // Battery blocks are read by readBasic() on their own tier, so they can
    // share a transaction with the registers either side of them.
    out.clear();
    return true;
}

// ===========================================================================
// Block parsers. `r` points at the first register of the block.
// ===========================================================================

void SolplanetASW::parseModbusAddress(const uint16_t *r, std::vector<TelemetryPoint> &out)
{
    // 31002: Modbus address (U16)
    if (!isNaN_U16(r[0]))
    {
        out.push_back(TP_FULL("modbus_address", "", String(r[0]), 31002, "U16", 1.0f));
    }
}

void SolplanetASW::parseGridCode(const uint16_t *r, std::vector<TelemetryPoint> &out)
{
    // 31027: Current grid code (E16)
    uint16_t gridCode = r[0];
    if (!isNaN_U16(gridCode))
    {
        out.push_back(TP_FULL("grid_code", "", String(gridCode), 31027, "E16", 1.0f));

        // Add human-readable grid code description
        String gridDesc = getGridCodeDescription(gridCode);
        if (gridDesc.length() > 0)
        {
            out.push_back(TP_FULL("grid_code_standard", "", gridDesc, 31027, "E16", 1.0f));
        }
    }
}

void SolplanetASW::parseRatedValues(const uint16_t *r, std::vector<TelemetryPoint> &out)
{
    // 31301 - Grid rated voltage (U16, V, scale 0.1)
    if (!isNaN_U16(r[0]))
    {
        float voltage = r[0] * 0.1f;
        out.push_back(TP_FULL("grid_rated_voltage", "V", String(voltage, 1), 31301, "U16", 0.1f));
    }

    // 31302 - Grid rated frequency (U16, Hz, scale 0.01)
    if (!isNaN_U16(r[1]))
    {
        float freq = r[1] * 0.01f;
        out.push_back(TP_FULL("grid_rated_frequency", "Hz", String(freq, 2), 31302, "U16", 0.01f));
    }
}

void SolplanetASW::parseEnergyCounters(const uint16_t *r, std::vector<TelemetryPoint> &out)
{
    // 31303-31304 - Today's energy (U32, kWh, scale 0.1)
    uint32_t todayEnergy = ((uint32_t)r[0] << 16) | r[1];
    if (!isNaN_U32(todayEnergy))
    {
        float energyKwh = todayEnergy * 0.1f;
        out.push_back(TP_FULL("energy_today", "kWh", String(energyKwh, 1), 31303, "U32", 0.1f));
    }

    // 31305-31306 - Total energy (U32, kWh, scale 0.1)
    uint32_t totalEnergy = ((uint32_t)r[2] << 16) | r[3];
    if (!isNaN_U32(totalEnergy))
    {
        float energyKwh = totalEnergy * 0.1f;
        out.push_back(TP_FULL("energy_total", "kWh", String(energyKwh, 1), 31305, "U32", 0.1f));
    }

    // 31307-31308 - Total hours (U32, hours, scale 1.0)
    uint32_t hours = ((uint32_t)r[4] << 16) | r[5];
    if (!isNaN_U32(hours))
    {
        out.push_back(TP_FULL("hours_total", "h", String(hours), 31307, "U32", 1.0f));
    }
}

void SolplanetASW::parseDeviceStatus(const uint16_t *r, std::vector<TelemetryPoint> &out)
{
    // Index 0: 31309 - Device state (E16)
    if (!isNaN_U16(r[0]))
    {
        uint16_t deviceState = r[0];
        out.push_back(TP_FULL("device_state", "", String(deviceState), 31309, "E16", 1.0f));
        String stateStr;
        switch (deviceState)
        {
        case 0: stateStr = "Wait"; break;
        case 1: stateStr = "Normal"; break;
        case 2: stateStr = "Fault"; break;
        case 4: stateStr = "Checking"; break;
        case 30: stateStr = "Dispatch"; break;
        case 40: stateStr = "Unknown40"; break;
        case 50: stateStr = "Over-Temp"; break;
        case 99: stateStr = "Stop"; break;
        case 170: stateStr = "Flash-Init"; break;
        default: stateStr = String(deviceState);
        }
        out.push_back(TP_FULL("device_state_text", "", stateStr, 31309, "E16", 1.0f));
    }

    // Index 1: 31310 - Connect time (U16, seconds)
    if (!isNaN_U16(r[1]))
    {
        out.push_back(TP_FULL("connect_time", "s", String(r[1]), 31310, "U16", 1.0f));
    }

    // Index 2: 31311 - Internal temperature (S16, °C, scale 0.1)
    int16_t tempInternal = (int16_t)r[2];
    if (!isNaN_S16(tempInternal))
    {
        float temperature = tempInternal * 0.1f;
        out.push_back(TP_FULL("temperature_internal", "C", String(temperature, 1), 31311, "S16", 0.1f));
    }

    // Index 3: 31312 - Inverter U phase temperature (S16, °C, scale 0.1)
    int16_t tempInverterU = (int16_t)r[3];
    if (!isNaN_S16(tempInverterU))
    {
        float temperature = tempInverterU * 0.1f;
        out.push_back(TP_FULL("temperature_inverter_u", "C", String(temperature, 1), 31312, "S16", 0.1f));
    }

    // Index 4-5: 31313-31314 - Unused/gaps, skip

    // Index 6: 31315 - Boost temperature (S16, °C, scale 0.1)
    int16_t tempBoost = (int16_t)r[6];
    if (!isNaN_S16(tempBoost))
    {
        float temperature = tempBoost * 0.1f;
        out.push_back(TP_FULL("temperature_boost", "C", String(temperature, 1), 31315, "S16", 0.1f));
    }

    // Index 7: 31316 - Unused/gap, skip

    // Index 8: 31317 - Bus voltage (U16, V, scale 0.1)
    if (!isNaN_U16(r[8]))
    {
        float voltage = r[8] * 0.1f;
        out.push_back(TP_FULL("bus_voltage", "V", String(voltage, 1), 31317, "U16", 0.1f));
    }
}

void SolplanetASW::parsePVInputs(const uint16_t *r, std::vector<TelemetryPoint> &out)
{
    // 31319-31338: PV1 through PV10 voltage/current pairs.
    // Parse MPPT inputs dynamically based on detected count
    for (uint8_t i = 0; i < mpptCount && i < 10; i++)
    {
        uint16_t pvBaseReg = 31319 + (i * 2);
        uint16_t voltage_raw = r[i * 2];
        uint16_t current_raw = r[i * 2 + 1];

        if (!isNaN_U16(voltage_raw) && !isNaN_U16(current_raw))
        {
            float pvVoltage = voltage_raw * 0.1f;
            float pvCurrent = current_raw * 0.01f;

            // Only add if PV input is actually connected (voltage > 10V)
            if (pvVoltage > 10.0f)
            {
                String pvNum = String(i + 1);
                out.push_back(TP_FULL("pv" + pvNum + "_voltage", "V", String(pvVoltage, 1), pvBaseReg, "U16", 0.1f));
                out.push_back(TP_FULL("pv" + pvNum + "_current", "A", String(pvCurrent, 2), pvBaseReg + 1, "U16", 0.01f));
                out.push_back(TP_FULL("pv" + pvNum + "_power", "W", String(pvVoltage * pvCurrent, 0), pvBaseReg, "calc", 1.0f));
            }
        }
    }
}

void SolplanetASW::parseStringCurrents(const uint16_t *r, std::vector<TelemetryPoint> &out)
{
    // 31339-31358: individual string currents (U16, A, scale 0.1)
    for (uint8_t i = 0; i < stringCount && i < 20; i++)
    {
        uint16_t stringReg = 31339 + i;
        if (!isNaN_U16(r[i]))
        {
            float current = r[i] * 0.1f;
            if (current > 0.1f)
            {
                String stringNum = String(i + 1);
                out.push_back(TP_FULL("string" + stringNum + "_current", "A", String(current, 1), stringReg, "U16", 0.1f));
            }
        }
    }
}

void SolplanetASW::parseGridAndPhase(const uint16_t *bulk, std::vector<TelemetryPoint> &out)
{
    // 31359-31379 (21 registers): 3-phase data, grid measurements, error/warning codes
    // === THREE-PHASE DATA (only if 3-phase inverter) ===
    if (isThreePhase)
    {
        // Index 0-5: 31359-31364 - L1/L2/L3 phase voltages and currents
        if (!isNaN_U16(bulk[0]))
        {
            float l1Voltage = bulk[0] * 0.1f;
            out.push_back(TP_FULL("l1_voltage", "V", String(l1Voltage, 1), 31359, "U16", 0.1f));
        }
        if (!isNaN_U16(bulk[1]))
        {
            float l1Current = bulk[1] * 0.1f;
            out.push_back(TP_FULL("l1_current", "A", String(l1Current, 1), 31360, "U16", 0.1f));
        }
        if (!isNaN_U16(bulk[2]))
        {
            float l2Voltage = bulk[2] * 0.1f;
            out.push_back(TP_FULL("l2_voltage", "V", String(l2Voltage, 1), 31361, "U16", 0.1f));
        }
        if (!isNaN_U16(bulk[3]))
        {
            float l2Current = bulk[3] * 0.1f;
            out.push_back(TP_FULL("l2_current", "A", String(l2Current, 1), 31362, "U16", 0.1f));
        }
        if (!isNaN_U16(bulk[4]))
        {
            float l3Voltage = bulk[4] * 0.1f;
            out.push_back(TP_FULL("l3_voltage", "V", String(l3Voltage, 1), 31363, "U16", 0.1f));
        }
        if (!isNaN_U16(bulk[5]))
        {
            float l3Current = bulk[5] * 0.1f;
            out.push_back(TP_FULL("l3_current", "A", String(l3Current, 1), 31364, "U16", 0.1f));
        }

        // Index 6-8: 31365-31367 - RS/RT/ST line voltages
        if (!isNaN_U16(bulk[6]))
        {
            float rsVoltage = bulk[6] * 0.1f;
            if (rsVoltage > 10.0f)
                out.push_back(TP_FULL("rs_line_voltage", "V", String(rsVoltage, 1), 31365, "U16", 0.1f));
        }
        if (!isNaN_U16(bulk[7]))
        {
            float rtVoltage = bulk[7] * 0.1f;
            if (rtVoltage > 10.0f)
                out.push_back(TP_FULL("rt_line_voltage", "V", String(rtVoltage, 1), 31366, "U16", 0.1f));
        }
        if (!isNaN_U16(bulk[8]))
        {
            float stVoltage = bulk[8] * 0.1f;
            if (stVoltage > 10.0f)
                out.push_back(TP_FULL("st_line_voltage", "V", String(stVoltage, 1), 31367, "U16", 0.1f));
        }
    }

    // === GRID MEASUREMENTS (all inverters) ===

    // Index 9: 31368 - Grid frequency (U16, Hz, scale 0.01)
    if (!isNaN_U16(bulk[9]))
    {
        float freq = bulk[9] * 0.01f;
        out.push_back(TP_FULL("grid_frequency", "Hz", String(freq, 2), 31368, "U16", 0.01f));
    }

    // Index 10-11: 31369-31370 - Apparent power (U32, VA, scale 1.0)
    uint32_t apparentPower = ((uint32_t)bulk[10] << 16) | bulk[11];
    if (!isNaN_U32(apparentPower))
    {
        out.push_back(TP_FULL("apparent_power", "VA", String(apparentPower), 31369, "U32", 1.0f));
    }

    // Index 12-13: 31371-31372 - Active power (S32, W, scale 1.0)
    int32_t activePower = (int32_t)(((uint32_t)bulk[12] << 16) | bulk[13]);
    if (!isNaN_S32(activePower))
    {
        out.push_back(TP_FULL("active_power", "W", String(activePower), 31371, "S32", 1.0f));
    }

    // Index 14-15: 31373-31374 - Reactive power (S32, Var, scale 1.0)
    int32_t reactivePower = (int32_t)(((uint32_t)bulk[14] << 16) | bulk[15]);
    if (!isNaN_S32(reactivePower))
    {
        out.push_back(TP_FULL("reactive_power", "Var", String(reactivePower), 31373, "S32", 1.0f));
    }

    // Index 16: 31375 - Power factor (S16, scale 0.01)
    int16_t pfSigned = (int16_t)bulk[16];
    if (!isNaN_S16(pfSigned))
    {
        float pf = pfSigned * 0.01f;
        out.push_back(TP_FULL("power_factor", "", String(pf, 2), 31375, "S16", 0.01f));
    }

    // Index 17: 31376 - Unused/gap, skip

    // Index 18: 31377 - Fault state (E16) - 0=No fault, 1=Internal fault
    if (!isNaN_U16(bulk[18]))
    {
        out.push_back(TP_FULL("fault_state", "", String(bulk[18]), 31377, "E16", 1.0f));
    }

    // Index 19: 31378 - Error message code (E16) - see MB001 section 3.4 for error codes
    if (!isNaN_U16(bulk[19]))
    {
        uint16_t errorCode = bulk[19];
        out.push_back(TP_FULL("error_code", "", String(errorCode), 31378, "E16", 1.0f));

        // Add human-readable error description
        String errorDesc = getErrorDescription(errorCode);
        if (errorDesc.length() > 0)
        {
            out.push_back(TP_FULL("error_message", "", errorDesc, 31378, "E16", 1.0f));
        }
    }

    // Index 20: 31379 - Warning message code (E16) - see MB001 section 3.4 for warning codes
    if (!isNaN_U16(bulk[20]))
    {
        uint16_t warningCode = bulk[20];
        out.push_back(TP_FULL("warning_code", "", String(warningCode), 31379, "E16", 1.0f));

        // Add human-readable warning description
        String warningDesc = getWarningDescription(warningCode);
        if (warningDesc.length() > 0)
        {
            out.push_back(TP_FULL("warning_message", "", warningDesc, 31379, "E16", 1.0f));
        }
    }
}

void SolplanetASW::parseGridConnection(const uint16_t *r, std::vector<TelemetryPoint> &out)
{
    // 31391: Grid connection status
    if (!isNaN_U16(r[0]))
    {
        out.push_back(TP_FULL("grid_connection", "", r[0] == 1 ? "Connected" : "Disconnected", 31391, "U16", 1.0f));
    }
}

void SolplanetASW::parsePVTotals(const uint16_t *r, std::vector<TelemetryPoint> &out)
{
    // 31601-31606: PV total power and PV-side energy (DC side, before inverter conversion)
    // Index 0-1: 31601-31602 - PV total power (U32, W, scale 1.0)
    uint32_t pvPower = ((uint32_t)r[0] << 16) | r[1];
    if (!isNaN_U32(pvPower))
    {
        out.push_back(TP_FULL("pv_total_power", "W", String(pvPower), 31601, "U32", 1.0f));
    }

    // Index 2-3: 31603-31604 - PV E-Today (U32, kWh, scale 0.1)
    // This is DC-side energy before inverter losses
    uint32_t pvEnergyToday = ((uint32_t)r[2] << 16) | r[3];
    if (!isNaN_U32(pvEnergyToday))
    {
        float energyKwh = pvEnergyToday * 0.1f;
        out.push_back(TP_FULL("pv_energy_today", "kWh", String(energyKwh, 1), 31603, "U32", 0.1f));
    }

    // Index 4-5: 31605-31606 - PV E-Total (U32, kWh, scale 0.1)
    // Total lifetime PV energy on DC side
    uint32_t pvEnergyTotal = ((uint32_t)r[4] << 16) | r[5];
    if (!isNaN_U32(pvEnergyTotal))
    {
        float energyKwh = pvEnergyTotal * 0.1f;
        out.push_back(TP_FULL("pv_energy_total", "kWh", String(energyKwh, 1), 31605, "U32", 0.1f));
    }
}

void SolplanetASW::parseBattery(const uint16_t *r, std::vector<TelemetryPoint> &out)
{
    // 31607-31629 (23 registers): BMS1 communication, status, voltage, current,
    // power, temperature, SOC, SOH, limits, charge/discharge energy
    // Index 0: 31607 - BMS1 Battery communication status (E16)
    // 0x000A=Normal, 0x0005=Error
    if (!isNaN_U16(r[0]))
    {
        out.push_back(TP_FULL("battery_comm_status", "", String(r[0], HEX), 31607, "E16", 1.0f));
    }

    // Index 1: 31608 - BMS1 Battery status (E16)
    // 0=N/A, 1=Idle, 2=Charging, 3=Discharging, 4=Error
    if (!isNaN_U16(r[1]))
    {
        uint16_t bmsStatus = r[1];
        out.push_back(TP_FULL("battery_status", "", String(bmsStatus), 31608, "E16", 1.0f));
        String statusText;
        switch (bmsStatus)
        {
        case 0: statusText = "N/A"; break;
        case 1: statusText = "Idle"; break;
        case 2: statusText = "Charging"; break;
        case 3: statusText = "Discharging"; break;
        case 4: statusText = "Error"; break;
        default: statusText = String(bmsStatus);
        }
        out.push_back(TP_FULL("battery_status_text", "", statusText, 31608, "E16", 1.0f));
    }

    // Index 2-9: 31609-31616 - Battery error/warning status registers (skip for now, detailed bit fields)

    // Index 10: 31617 - BMS1 Battery voltage (U16, V, scale 0.01)
    if (!isNaN_U16(r[10]))
    {
        float voltage = r[10] * 0.01f;
        out.push_back(TP_FULL("battery_voltage", "V", String(voltage, 2), 31617, "U16", 0.01f));
    }

    // Index 11: 31618 - BMS1 Battery current (S16, A, scale 0.1)
    // Positive = charging, Negative = discharging
    int16_t currentSigned = (int16_t)r[11];
    if (!isNaN_S16(currentSigned))
    {
        float current = currentSigned * 0.1f;
        out.push_back(TP_FULL("battery_current", "A", String(current, 1), 31618, "S16", 0.1f));
    }

    // Index 12-13: 31619-31620 - BMS1 Battery power (S32, W, scale 1.0)
    int32_t power = (int32_t)(((uint32_t)r[12] << 16) | r[13]);
    if (!isNaN_S32(power))
    {
        out.push_back(TP_FULL("battery_power", "W", String(power), 31619, "S32", 1.0f));
    }

    // Index 14: 31621 - BMS1 Battery temperature (S16, °C, scale 0.1)
    int16_t tempSigned = (int16_t)r[14];
    if (!isNaN_S16(tempSigned))
    {
        float temperature = tempSigned * 0.1f;
        out.push_back(TP_FULL("battery_temperature", "C", String(temperature, 1), 31621, "S16", 0.1f));
    }

    // Index 15: 31622 - BMS1 Battery SOC (U16, %)
    // NOTE: Modbus doc says scale 0.01, but actual register returns 0-100 directly (not 0-10000)
    if (!isNaN_U16(r[15]))
    {
        out.push_back(TP_FULL("battery_soc", "%", String(r[15]), 31622, "U16", 1.0f));
    }

    // Index 16: 31623 - BMS1 Battery SOH (U16, %)
    // Same issue as SOC - returns 0-100 directly
    if (!isNaN_U16(r[16]))
    {
        out.push_back(TP_FULL("battery_soh", "%", String(r[16]), 31623, "U16", 1.0f));
    }

    // Index 17: 31624 - BMS1 Battery charging current limit (U16, A, scale 0.1)
    if (!isNaN_U16(r[17]))
    {
        float limit = r[17] * 0.1f;
        out.push_back(TP_FULL("battery_charge_limit", "A", String(limit, 1), 31624, "U16", 0.1f));
    }

    // Index 18: 31625 - BMS1 Battery discharge current limit (U16, A, scale 0.1)
    if (!isNaN_U16(r[18]))
    {
        float limit = r[18] * 0.1f;
        out.push_back(TP_FULL("battery_discharge_limit", "A", String(limit, 1), 31625, "U16", 0.1f));
    }

    // Index 19-20: 31626-31627 - BMS1 Battery E-Charge-Today (U32, kWh, scale 0.1)
    uint32_t chargeToday = ((uint32_t)r[19] << 16) | r[20];
    if (!isNaN_U32(chargeToday))
    {
        float energyKwh = chargeToday * 0.1f;
        out.push_back(TP_FULL("battery_charge_today", "kWh", String(energyKwh, 1), 31626, "U32", 0.1f));
    }

    // Index 21-22: 31628-31629 - BMS1 Battery E-Discharge-Today (U32, kWh, scale 0.1)
    uint32_t dischargeToday = ((uint32_t)r[21] << 16) | r[22];
    if (!isNaN_U32(dischargeToday))
    {
        float energyKwh = dischargeToday * 0.1f;
        out.push_back(TP_FULL("battery_discharge_today", "kWh", String(energyKwh, 1), 31628, "U32", 0.1f));
    }
}

void SolplanetASW::parseAcEnergy(const uint16_t *r, std::vector<TelemetryPoint> &out)
{
    // 31630-31633: consumption and generation today at the AC side
    // Index 0-1: 31630-31631 - E-Consumption-Today at AC side (U32, kWh, scale 0.1)
    uint32_t consumptionToday = ((uint32_t)r[0] << 16) | r[1];
    if (!isNaN_U32(consumptionToday))
    {
        float energyKwh = consumptionToday * 0.1f;
        out.push_back(TP_FULL("consumption_today", "kWh", String(energyKwh, 1), 31630, "U32", 0.1f));
    }

    // Index 2-3: 31632-31633 - E-Generation-Today at AC side (U32, kWh, scale 0.1)
    uint32_t generationToday = ((uint32_t)r[2] << 16) | r[3];
    if (!isNaN_U32(generationToday))
    {
        float energyKwh = generationToday * 0.1f;
        out.push_back(TP_FULL("generation_today_ac", "kWh", String(energyKwh, 1), 31632, "U32", 0.1f));
    }
}

void SolplanetASW::parseEpsBasic(const uint16_t *r, std::vector<TelemetryPoint> &out)
{
    // 31634-31636: EPS (backup output) load voltage, current, frequency
    // Index 0: 31634 - EPS load voltage (U16, V, scale 0.1)
    if (!isNaN_U16(r[0]))
    {
        float voltage = r[0] * 0.1f;
        if (voltage > 10.0f)  // Only publish if backup is active
        {
            out.push_back(TP_FULL("eps_voltage", "V", String(voltage, 1), 31634, "U16", 0.1f));
        }
    }

    // Index 1: 31635 - EPS load current (U16, A, scale 0.1)
    if (!isNaN_U16(r[1]))
    {
        float current = r[1] * 0.1f;
        out.push_back(TP_FULL("eps_current", "A", String(current, 1), 31635, "U16", 0.1f));
    }

    // Index 2: 31636 - EPS load frequency (U16, Hz, scale 0.01)
    if (!isNaN_U16(r[2]))
    {
        float freq = r[2] * 0.01f;
        if (freq > 10.0f)  // Only publish if backup is active
        {
            out.push_back(TP_FULL("eps_frequency", "Hz", String(freq, 2), 31636, "U16", 0.01f));
        }
    }
}

void SolplanetASW::parseEpsPower(const uint16_t *r, std::vector<TelemetryPoint> &out)
{
    // 31637-31644: EPS load power and energy
    // Index 0-1: 31637-31638 - EPS load active power (S32, W, scale 1.0)
    int32_t epsActivePower = (int32_t)(((uint32_t)r[0] << 16) | r[1]);
    if (!isNaN_S32(epsActivePower))
    {
        out.push_back(TP_FULL("eps_active_power", "W", String(epsActivePower), 31637, "S32", 1.0f));
    }

    // Index 2-3: 31639-31640 - EPS load reactive power (S32, Var, scale 1.0)
    int32_t epsReactivePower = (int32_t)(((uint32_t)r[2] << 16) | r[3]);
    if (!isNaN_S32(epsReactivePower))
    {
        out.push_back(TP_FULL("eps_reactive_power", "Var", String(epsReactivePower), 31639, "S32", 1.0f));
    }

    // Index 4-5: 31641-31642 - E-Consumption-Today at EPS load side (U32, kWh, scale 0.1)
    uint32_t epsConsumptionToday = ((uint32_t)r[4] << 16) | r[5];
    if (!isNaN_U32(epsConsumptionToday))
    {
        float energyKwh = epsConsumptionToday * 0.1f;
        out.push_back(TP_FULL("eps_consumption_today", "kWh", String(energyKwh, 1), 31641, "U32", 0.1f));
    }

    // Index 6-7: 31643-31644 - E-Consumption-Total at EPS load side (U32, kWh, scale 0.1)
    uint32_t epsConsumptionTotal = ((uint32_t)r[6] << 16) | r[7];
    if (!isNaN_U32(epsConsumptionTotal))
    {
        float energyKwh = epsConsumptionTotal * 0.1f;
        out.push_back(TP_FULL("eps_consumption_total", "kWh", String(energyKwh, 1), 31643, "U32", 0.1f));
    }
}

void SolplanetASW::parseEpsPhases(const uint16_t *r, std::vector<TelemetryPoint> &out)
{
    // 31645-31662 (18 registers): EPS per-phase measurements (3-phase backup systems)
    // Index 0-5: 31645-31650 - Phase 1/2/3 voltage and current for EPS Load
    if (!isNaN_U16(r[0]))
    {
        float voltage = r[0] * 0.1f;
        if (voltage > 10.0f)
            out.push_back(TP_FULL("eps_l1_voltage", "V", String(voltage, 1), 31645, "U16", 0.1f));
    }
    if (!isNaN_U16(r[1]))
    {
        float current = r[1] * 0.1f;
        out.push_back(TP_FULL("eps_l1_current", "A", String(current, 1), 31646, "U16", 0.1f));
    }
    if (!isNaN_U16(r[2]))
    {
        float voltage = r[2] * 0.1f;
        if (voltage > 10.0f)
            out.push_back(TP_FULL("eps_l2_voltage", "V", String(voltage, 1), 31647, "U16", 0.1f));
    }
    if (!isNaN_U16(r[3]))
    {
        float current = r[3] * 0.1f;
        out.push_back(TP_FULL("eps_l2_current", "A", String(current, 1), 31648, "U16", 0.1f));
    }
    if (!isNaN_U16(r[4]))
    {
        float voltage = r[4] * 0.1f;
        if (voltage > 10.0f)
            out.push_back(TP_FULL("eps_l3_voltage", "V", String(voltage, 1), 31649, "U16", 0.1f));
    }
    if (!isNaN_U16(r[5]))
    {
        float current = r[5] * 0.1f;
        out.push_back(TP_FULL("eps_l3_current", "A", String(current, 1), 31650, "U16", 0.1f));
    }

    // Index 6-17: 31651-31662 - Phase 1/2/3 active & reactive power for EPS Load
    // Phase 1 active power
    uint32_t epsL1Power = ((uint32_t)r[6] << 16) | r[7];
    if (!isNaN_U32(epsL1Power))
    {
        out.push_back(TP_FULL("eps_l1_active_power", "W", String(epsL1Power), 31651, "U32", 1.0f));
    }
    // Phase 1 reactive power
    int32_t epsL1Reactive = (int32_t)(((uint32_t)r[8] << 16) | r[9]);
    if (!isNaN_S32(epsL1Reactive))
    {
        out.push_back(TP_FULL("eps_l1_reactive_power", "Var", String(epsL1Reactive), 31653, "S32", 1.0f));
    }

    // Phase 2 active power
    uint32_t epsL2Power = ((uint32_t)r[10] << 16) | r[11];
    if (!isNaN_U32(epsL2Power))
    {
        out.push_back(TP_FULL("eps_l2_active_power", "W", String(epsL2Power), 31655, "U32", 1.0f));
    }
    // Phase 2 reactive power
    int32_t epsL2Reactive = (int32_t)(((uint32_t)r[12] << 16) | r[13]);
    if (!isNaN_S32(epsL2Reactive))
    {
        out.push_back(TP_FULL("eps_l2_reactive_power", "Var", String(epsL2Reactive), 31657, "S32", 1.0f));
    }

    // Phase 3 active power
    uint32_t epsL3Power = ((uint32_t)r[14] << 16) | r[15];
    if (!isNaN_U32(epsL3Power))
    {
        out.push_back(TP_FULL("eps_l3_active_power", "W", String(epsL3Power), 31659, "U32", 1.0f));
    }
    // Phase 3 reactive power
    int32_t epsL3Reactive = (int32_t)(((uint32_t)r[16] << 16) | r[17]);
    if (!isNaN_S32(epsL3Reactive))
    {
        out.push_back(TP_FULL("eps_l3_reactive_power", "Var", String(epsL3Reactive), 31661, "S32", 1.0f));
    }
}

void SolplanetASW::parseGridPhasePower(const uint16_t *r, std::vector<TelemetryPoint> &out)
{
    // 31663-31674: grid active and reactive power per phase (3-phase only)
    // Index 0-1: 31663-31664 - Phase 1 active power for Grid (U32, W, scale 1.0)
    uint32_t gridL1Power = ((uint32_t)r[0] << 16) | r[1];
    if (!isNaN_U32(gridL1Power))
    {
        out.push_back(TP_FULL("grid_l1_power", "W", String(gridL1Power), 31663, "U32", 1.0f));
    }

    // Index 2-3: 31665-31666 - Phase 1 reactive power for Grid (S32, Var, scale 1.0)
    int32_t gridL1Reactive = (int32_t)(((uint32_t)r[2] << 16) | r[3]);
    if (!isNaN_S32(gridL1Reactive))
    {
        out.push_back(TP_FULL("grid_l1_reactive", "Var", String(gridL1Reactive), 31665, "S32", 1.0f));
    }

    // Index 4-5: 31667-31668 - Phase 2 active power for Grid (U32, W, scale 1.0)
    uint32_t gridL2Power = ((uint32_t)r[4] << 16) | r[5];
    if (!isNaN_U32(gridL2Power))
    {
        out.push_back(TP_FULL("grid_l2_power", "W", String(gridL2Power), 31667, "U32", 1.0f));
    }

    // Index 6-7: 31669-31670 - Phase 2 reactive power for Grid (S32, Var, scale 1.0)
    int32_t gridL2Reactive = (int32_t)(((uint32_t)r[6] << 16) | r[7]);
    if (!isNaN_S32(gridL2Reactive))
    {
        out.push_back(TP_FULL("grid_l2_reactive", "Var", String(gridL2Reactive), 31669, "S32", 1.0f));
    }

    // Index 8-9: 31671-31672 - Phase 3 active power for Grid (U32, W, scale 1.0)
    uint32_t gridL3Power = ((uint32_t)r[8] << 16) | r[9];
    if (!isNaN_U32(gridL3Power))
    {
        out.push_back(TP_FULL("grid_l3_power", "W", String(gridL3Power), 31671, "U32", 1.0f));
    }

    // Index 10-11: 31673-31674 - Phase 3 reactive power for Grid (S32, Var, scale 1.0)
    int32_t gridL3Reactive = (int32_t)(((uint32_t)r[10] << 16) | r[11]);
    if (!isNaN_S32(gridL3Reactive))
    {
        out.push_back(TP_FULL("grid_l3_reactive", "Var", String(gridL3Reactive), 31673, "S32", 1.0f));
    }
}

void SolplanetASW::parseGridExport(const uint16_t *r, std::vector<TelemetryPoint> &out)
{
    // 31675-31678: grid export energy today and total
    // Index 0-1: 31675-31676 - Energy charge today for Grid (U32, kWh, scale 0.1)
    uint32_t gridExportToday = ((uint32_t)r[0] << 16) | r[1];
    if (!isNaN_U32(gridExportToday))
    {
        float energyKwh = gridExportToday * 0.1f;
        out.push_back(TP_FULL("grid_export_today", "kWh", String(energyKwh, 1), 31675, "U32", 0.1f));
    }

    // Index 2-3: 31677-31678 - Energy charge total for Grid (U32, kWh, scale 0.1)
    uint32_t gridExportTotal = ((uint32_t)r[2] << 16) | r[3];
    if (!isNaN_U32(gridExportTotal))
    {
        float energyKwh = gridExportTotal * 0.1f;
        out.push_back(TP_FULL("grid_export_total", "kWh", String(energyKwh, 1), 31677, "U32", 0.1f));
    }
}

void SolplanetASW::parseBmsDiagnostics(const uint16_t *r, std::vector<TelemetryPoint> &out)
{
    // 31679-31681: BMS1 advanced diagnostics
    // Index 0: 31679 - BMS1 Insulation resistance (U16, kΩ, scale 1.0)
    if (!isNaN_U16(r[0]))
    {
        out.push_back(TP_FULL("battery_insulation_resistance", "kOhm", String(r[0]), 31679, "U16", 1.0f));
    }

    // Index 1: 31680 - BMS1 Charge/discharge cycles (U16, scale 1.0)
    if (!isNaN_U16(r[1]))
    {
        out.push_back(TP_FULL("battery_cycles", "", String(r[1]), 31680, "U16", 1.0f));
    }

    // Index 2: 31681 - BMS1 Environment temperature (U16, °C, scale 0.1)
    if (!isNaN_U16(r[2]))
    {
        float envTemp = r[2] * 0.1f;
        out.push_back(TP_FULL("battery_env_temperature", "C", String(envTemp, 1), 31681, "U16", 0.1f));
    }
}

void SolplanetASW::parseGenerator(const uint16_t *r, std::vector<TelemetryPoint> &out)
{
    // 31685-31687: diesel generator power and energy
    // Index 0-1: 31685-31686 - Oil engine power (S32, W, scale 1.0)
    int32_t genPower = (int32_t)(((uint32_t)r[0] << 16) | r[1]);
    if (!isNaN_S32(genPower))
    {
        out.push_back(TP_FULL("generator_power", "W", String(genPower), 31685, "S32", 1.0f));
    }

    // Index 2: 31687 - The power of the daily oil engine (U32, kWh, scale 0.1)
    // Note: Register 31687 is a single U16, not U32 as documented - treating as U16 for now
    if (!isNaN_U16(r[2]))
    {
        float genEnergyKwh = r[2] * 0.1f;
        out.push_back(TP_FULL("generator_energy_today", "kWh", String(genEnergyKwh, 1), 31687, "U16", 0.1f));
    }
}

String SolplanetASW::getErrorDescription(uint16_t errorCode) const
{
    // Error codes from MB001_ASW GEN-Modbus section 3.4
    // Use PROGMEM strings to save RAM
    switch (errorCode)
    {
    case 0: return F("No Error");
    case 10: return F("Device Fault");
    case 49: return F("PV1 Lightning Arrester Fault");
    case 50: return F("PV2 Lightning Arrester Fault");
    case 52: return F("Neutral Line Loss Fault");
    case 56: return F("GFCI Protect Fault (30mA)");
    case 57: return F("GFCI Protect Fault (60mA)");
    case 58: return F("GFCI Protect Fault (150mA)");
    case 63: return F("L-PE Short-Circuit Protection");
    case 64: return F("PV Input Mode Error");
    case 66: return F("PV1 Reverse Connection Fault");
    case 67: return F("PV2 Reverse Connection Fault");
    case 68: return F("PV3 Reverse Connection Fault");
    default: return String(""); // Unknown error code - return empty string
    }
}

String SolplanetASW::getWarningDescription(uint16_t warningCode) const
{
    // Warning codes from MB001_ASW GEN-Modbus section 3.4
    // Use PROGMEM strings to save RAM
    switch (warningCode)
    {
    case 0: return F("No Warning");
    case 30: return F("Recover From Warning");
    case 150: return F("SPD Damaged");
    case 156: return F("Internal Fan Warning");
    case 157: return F("External Fan Warning");
    case 163: return F("String Current Abnormal");
    case 165: return F("Ground Connect Warning");
    case 166: return F("CPU Self-Test: Register Abnormal");
    case 167: return F("CPU Self-Test: RAM Abnormal");
    case 168: return F("CPU Self-Test: ROM Abnormal");
    case 174: return F("Low Air Temperature");
    case 175: return F("Battery SOC Low");
    case 176: return F("Battery Fault Status");
    case 177: return F("Battery Communication Disconnect");
    case 178: return F("EPS Output Over");
    default: return String(""); // Unknown warning code - return empty string
    }
}

String SolplanetASW::getGridCodeDescription(uint16_t gridCode) const
{
    // Grid codes from MB001_ASW GEN-Modbus section 3.5
    // These are country/region-specific grid connection standards
    // Use PROGMEM strings to save RAM (~2KB of strings here)
    switch (gridCode)
    {
    case 3: return F("Default");
    case 8: return F("GR PPC");
    case 35: return F("NB/T32004:2018 (China)");
    case 47: return F("AU AS 4777.2:2015 (Australia)");
    case 48: return F("NZ AS 4777.2:2015 (New Zealand)");
    case 49: return F("EN50549-1 50Hz (Europe)");
    case 50: return F("EN50549-1 60Hz (Europe)");
    case 51: return F("TOR Erzeuger Typ A V1.1 (Germany)");
    case 59: return F("CNS15382:2018 (Taiwan)");
    case 64: return F("EN 50549-1 (Europe)");
    case 65: return F("NL EN50549-1:2019 (Netherlands)");
    case 66: return F("BR NBR 16149:2013 (Brazil)");
    case 67: return F("VDE0126-1-1/A1/VFR (Germany)");
    case 68: return F("IEC 61727 50Hz");
    case 69: return F("C10/11:2019 (Belgium)");
    case 70: return F("VDE-AR-N4105:2018 (Germany)");
    case 71: return F("IEC 61727 60Hz");
    case 72: return F("G98/1 (UK)");
    case 73: return F("G99/1 (UK)");
    case 74: return F("AS/NZS4777.2:2020 Type A (Australia)");
    case 75: return F("AS/NZS4777.2:2020 Type B (Australia)");
    case 76: return F("AS/NZS4777.2:2020 Type C (Australia)");
    case 77: return F("AS/NZS4777.2:2020 (New Zealand)");
    case 78: return F("IL SI4777.3 (Israel)");
    case 79: return F("KR KS C 8565:2020 (South Korea)");
    case 80: return F("ES UNE206007-1 (Spain)");
    case 81: return F("CY EN50549-1 (Cyprus)");
    case 82: return F("CS PPDS A1 (Czech Republic)");
    case 83: return F("PL EN50549-1 (Poland)");
    case 84: return F("CEI 0-21:2019 (Italy)");
    case 85: return F("DK EN50549-1 (Denmark)");
    case 86: return F("CH NA/EEA-NE7 (Switzerland)");
    case 89: return F("RO Order208 (Romania)");
    case 90: return F("SI EN50549-1 (Slovenia)");
    case 91: return F("LV EN50549-1 (Latvia)");
    case 92: return F("VDE0126/VFR2019 IS 50Hz (Iceland)");
    case 93: return F("VDE0126/VFR2019 IS 60Hz (Iceland)");
    case 94: return F("ZA NRS 097-2-1:2017 (South Africa)");
    case 95: return F("BR PORTARIA No.140 (Brazil)");
    case 96: return F("NTS 631 Type A (Thailand)");
    case 97: return F("NTS 631 Type B (Thailand)");
    case 98: return F("NO EN50549-1 (Norway)");
    case 99: return F("VDE-AR-N 4110 (Germany)");
    case 100: return F("EN 50549-2 (Europe)");
    case 101: return F("DEWA:2016 (UAE)");
    case 102: return F("DK1 EN50549-1 (Denmark)");
    case 103: return F("ZA RPPs (South Africa)");
    default: return String(""); // Unknown grid code - return empty string
    }
}
