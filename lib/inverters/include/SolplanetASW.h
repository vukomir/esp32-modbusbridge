#pragma once

#include <Arduino.h>
#include <vector>
#include "InverterInterface.h"
#include "Config.h"
#include "ModbusClient.h"

/**
 * @brief SolPlanet ASW (GEN & HYBRID) driver with tiered polling.
 *
 * The register map is described as a table of contiguous blocks, each assigned
 * to a polling tier. Live electrical values (PV, grid, house/EPS load) are read
 * on every cycle; status, battery, energy counters and constants are read at
 * progressively longer intervals. Blocks that are due in the same cycle and sit
 * next to each other on the register map are fetched in one Modbus transaction.
 *
 * This is what makes a 1 second poll interval reachable: a full sweep of the
 * map is ~9 transactions and well over a second at 9600 baud, while the fast
 * tier alone is 3.
 */
class SolplanetASW : public InverterInterface
{
public:
    explicit SolplanetASW(ModbusClient &modbus, Config &config);
    bool begin() override;

    // Reads every block that is due this cycle (battery blocks included when
    // read_storage_regs is on) and appends their telemetry points to `out`.
    // Returns false if a required live block (PV, grid) could not be read.
    bool readBasic(std::vector<TelemetryPoint> &out) override;

    // Battery blocks are scheduled by readBasic alongside everything else so
    // they can share transactions with their neighbours. Kept for the
    // InverterInterface contract: always succeeds with no points.
    bool readStorage(std::vector<TelemetryPoint> &out) override;

    // Polling tiers. Fast is every cycle (poll_interval_sec). Medium and Slow
    // come from poll_medium_interval_sec / poll_slow_interval_sec; Slower and
    // Static are fixed multiples of Slow (x2 and x10).
    enum Tier : uint8_t { TIER_FAST, TIER_MEDIUM, TIER_SLOW, TIER_SLOWER, TIER_STATIC };

    // Largest number of registers fetched in one transaction. 64 registers is a
    // 133-byte reply, which stays inside the UART driver's default RX buffer.
    static constexpr uint8_t MAX_SPAN = 64;
    // Floor for the adaptive span reduction (see longFrameStrikes).
    static constexpr uint8_t MIN_SPAN = 24;

    // A block the inverter rejects with a Modbus exception (or that fails
    // UNSUPPORTED_AFTER_FAILURES times running, if it is not a required block)
    // is left alone for this long before it is probed again.
    static constexpr uint32_t UNSUPPORTED_RETRY_MS = 600000;
    static constexpr uint8_t UNSUPPORTED_AFTER_FAILURES = 3;

private:
    ModbusClient &modbus;
    Config &config;
    uint8_t slaveAddr;
    bool isThreePhase;
    bool phaseDetected;
    uint8_t mpptCount;      // Number of MPPT/PV inputs (from register 31074)
    uint8_t stringCount;    // Number of individual strings (from register 31075)
    bool configDetected;    // True after reading device configuration

    // ---- Tiered block scheduler -------------------------------------------

    typedef void (SolplanetASW::*BlockParser)(const uint16_t *r, std::vector<TelemetryPoint> &out);

    enum BlockFlags : uint8_t
    {
        BLOCK_REQUIRED = 0x01,    // failing to read it fails the poll
        BLOCK_THREE_PHASE = 0x02, // only on 3-phase inverters
        BLOCK_STRINGS = 0x04,     // only when the inverter reports string currents
        BLOCK_STORAGE = 0x08      // only when read_storage_regs is enabled
    };

    struct Block
    {
        uint16_t reg;       // first register, 3xxxx numbering
        uint8_t count;      // registers in the block
        uint8_t tier;       // Tier
        uint8_t flags;      // BlockFlags
        BlockParser parse;  // turns the raw words into telemetry points

        // Scheduling state
        uint32_t nextDueMs;
        bool scheduled;     // nextDueMs is valid
        bool everRead;      // has been read successfully at least once
        bool unsupported;   // parked; re-probed every UNSUPPORTED_RETRY_MS
        uint8_t failStreak; // consecutive failed reads
    };

    static constexpr uint8_t MAX_BLOCKS = 24;
    std::vector<Block> blocks; // sorted by register address

    uint32_t baseIntervalMs;   // poll_interval_sec
    uint32_t mediumIntervalMs; // poll_medium_interval_sec
    uint32_t slowIntervalMs;   // poll_slow_interval_sec

    // Merging neighbouring blocks into one transaction is switched off if the
    // inverter turns out to reject a merged read whose parts it accepts.
    bool coalesce;
    // Current cap on registers per transaction. Halved (down to MIN_SPAN) when
    // long merged reads keep failing on the wire while their parts succeed -
    // on a noisy bus a longer frame is simply a bigger target.
    uint8_t maxSpan;
    uint8_t longFrameStrikes;

    void addBlock(uint16_t reg, uint8_t count, Tier tier, uint8_t flags, BlockParser parse);
    bool blockEnabled(const Block &b, bool storageEnabled) const;
    bool blockDue(const Block &b, uint32_t now) const;
    uint32_t tierPeriodMs(uint8_t tier) const;
    // Index one past the last block of the run starting at `first`.
    size_t runEnd(const bool *selected, size_t first) const;
    uint32_t estimateCycleMs(const bool *selected) const;
    bool readBlockAlone(Block &b, uint32_t now, std::vector<TelemetryPoint> &out);
    void markRead(Block &b, uint32_t now);
    void markFailed(Block &b, uint32_t now, ModbusReadError error);

    // Helper methods
    uint16_t inputRegisterToModbus(uint16_t inputRegister) const;
    bool detectPhaseConfiguration();
    bool detectDeviceConfiguration(); // Reads MPPT count, string count, etc.

    // NaN value checking (per MB001 spec)
    inline bool isNaN_U16(uint16_t val) const { return val == 0xFFFF; }
    inline bool isNaN_S16(int16_t val) const { return val == (int16_t)0x8000; }
    inline bool isNaN_U32(uint32_t val) const { return val == 0xFFFFFFFF; }
    inline bool isNaN_S32(int32_t val) const { return val == (int32_t)0x80000000; }

    // Block parsers. `r` points at the block's first register.
    void parseModbusAddress(const uint16_t *r, std::vector<TelemetryPoint> &out);   // 31002
    void parseGridCode(const uint16_t *r, std::vector<TelemetryPoint> &out);        // 31027
    void parseRatedValues(const uint16_t *r, std::vector<TelemetryPoint> &out);     // 31301-31302
    void parseEnergyCounters(const uint16_t *r, std::vector<TelemetryPoint> &out);  // 31303-31308
    void parseDeviceStatus(const uint16_t *r, std::vector<TelemetryPoint> &out);    // 31309-31317
    void parsePVInputs(const uint16_t *r, std::vector<TelemetryPoint> &out);        // 31319-31338
    void parseStringCurrents(const uint16_t *r, std::vector<TelemetryPoint> &out);  // 31339-31358
    void parseGridAndPhase(const uint16_t *r, std::vector<TelemetryPoint> &out);    // 31359-31379
    void parseGridConnection(const uint16_t *r, std::vector<TelemetryPoint> &out);  // 31391
    void parsePVTotals(const uint16_t *r, std::vector<TelemetryPoint> &out);        // 31601-31606
    void parseBattery(const uint16_t *r, std::vector<TelemetryPoint> &out);         // 31607-31629
    void parseAcEnergy(const uint16_t *r, std::vector<TelemetryPoint> &out);        // 31630-31633
    void parseEpsBasic(const uint16_t *r, std::vector<TelemetryPoint> &out);        // 31634-31636
    void parseEpsPower(const uint16_t *r, std::vector<TelemetryPoint> &out);        // 31637-31644
    void parseEpsPhases(const uint16_t *r, std::vector<TelemetryPoint> &out);       // 31645-31662
    void parseGridPhasePower(const uint16_t *r, std::vector<TelemetryPoint> &out);  // 31663-31674
    void parseGridExport(const uint16_t *r, std::vector<TelemetryPoint> &out);      // 31675-31678
    void parseBmsDiagnostics(const uint16_t *r, std::vector<TelemetryPoint> &out);  // 31679-31681
    void parseGenerator(const uint16_t *r, std::vector<TelemetryPoint> &out);       // 31685-31687

    // Code translation (MB001 sections 3.4, 3.5)
    String getErrorDescription(uint16_t errorCode) const;
    String getWarningDescription(uint16_t warningCode) const;
    String getGridCodeDescription(uint16_t gridCode) const;
};
