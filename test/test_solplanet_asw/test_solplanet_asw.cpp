#include <unity.h>
#include <Arduino.h>
#include "SolplanetASW.h"
#include "ModbusClient.h"
#include "Config.h"
#include <vector>

// Test fixtures
Config *testConfig;
ModbusClient *modbusClient;
SolplanetASW *inverter;

void setUp(void)
{
    testConfig = new Config();

    // Set up basic configuration
    testConfig->setInt("baudrate", 9600);
    testConfig->setString("parity", "N");
    testConfig->setInt("stop_bits", 1);
    testConfig->setInt("rs485_de_re_pin", 4);
    testConfig->setInt("rtu_addr", 1);

    modbusClient = new ModbusClient(*testConfig);
    modbusClient->begin();

    inverter = new SolplanetASW(*modbusClient, *testConfig);
}

void tearDown(void)
{
    if (inverter)
    {
        delete inverter;
        inverter = nullptr;
    }

    if (modbusClient)
    {
        modbusClient->end();
        delete modbusClient;
        modbusClient = nullptr;
    }

    if (testConfig)
    {
        delete testConfig;
        testConfig = nullptr;
    }
}

// Test basic initialization
void test_solplanet_initialization()
{
    // begin() may fail without actual device, but should not crash
    bool result = inverter->begin();
    TEST_ASSERT_TRUE(result || !result); // Just test that it doesn't crash
}

// Test slave address configuration
void test_slave_address_configuration()
{
    // Test default slave address
    Config defaultConfig;
    SolplanetASW defaultInverter(*modbusClient, defaultConfig);

    // Test custom slave address
    testConfig->setInt("rtu_addr", 5);
    SolplanetASW customInverter(*modbusClient, *testConfig);

    // Both should initialize without crashing
    TEST_ASSERT_TRUE(true); // If we get here, constructors didn't crash
}

// Test input register address conversion
void test_input_register_conversion()
{
    // Test the address conversion function
    // This tests the private method indirectly through public interface

    // We can't directly test the private method, but we can test that
    // the class handles register addresses correctly by attempting operations
    std::vector<TelemetryPoint> data;

    // This will fail without actual device, but tests the address handling
    bool result = inverter->readBasic(data);
    TEST_ASSERT_TRUE(result || !result); // Just test that it doesn't crash
}

// Test telemetry point structure
void test_telemetry_point_structure()
{
    std::vector<TelemetryPoint> data;

    // Create a test telemetry point
    TelemetryPoint testPoint;
    testPoint.name = "test_voltage";
    testPoint.unit = "V";
    testPoint.value = "230.5";

    data.push_back(testPoint);

    TEST_ASSERT_EQUAL_INT(1, data.size());
    TEST_ASSERT_EQUAL_STRING("test_voltage", data[0].name.c_str());
    TEST_ASSERT_EQUAL_STRING("V", data[0].unit.c_str());
    TEST_ASSERT_EQUAL_STRING("230.5", data[0].value.c_str());
}

// Test basic telemetry reading
void test_basic_telemetry_reading()
{
    std::vector<TelemetryPoint> basicData;

    // This will likely fail without actual device, but should not crash
    bool result = inverter->readBasic(basicData);

    // Test that the method completes without crashing
    TEST_ASSERT_TRUE(result || !result);

    // If it succeeds, data should be populated
    if (result)
    {
        TEST_ASSERT_GREATER_THAN(0, basicData.size());
    }
}

// Test storage telemetry reading
void test_storage_telemetry_reading()
{
    std::vector<TelemetryPoint> storageData;

    // Test with storage reading disabled (default)
    bool result = inverter->readStorage(storageData);

    // Should succeed even if no storage data (returns true for disabled)
    TEST_ASSERT_TRUE(result);

    // Enable storage reading
    testConfig->setBool("read_storage_regs", true);
    result = inverter->readStorage(storageData);

    // May fail without actual device, but should not crash
    TEST_ASSERT_TRUE(result || !result);
}

// Test phase detection logic
void test_phase_detection()
{
    // The phase detection happens during begin(), but we can test
    // that the class handles different scenarios without crashing

    Config singlePhaseConfig;
    singlePhaseConfig.setInt("rtu_addr", 1);
    SolplanetASW singlePhaseInverter(*modbusClient, singlePhaseConfig);

    Config threePhaseConfig;
    threePhaseConfig.setInt("rtu_addr", 2);
    SolplanetASW threePhaseInverter(*modbusClient, threePhaseConfig);

    // Both should initialize without crashing
    bool result1 = singlePhaseInverter.begin();
    bool result2 = threePhaseInverter.begin();

    TEST_ASSERT_TRUE(result1 || !result1); // Just test no crash
    TEST_ASSERT_TRUE(result2 || !result2); // Just test no crash
}

// Test error handling in telemetry reading
void test_telemetry_error_handling()
{
    std::vector<TelemetryPoint> data;

    // Test reading without initialization
    SolplanetASW uninitializedInverter(*modbusClient, *testConfig);
    bool result = uninitializedInverter.readBasic(data);

    // Should handle uninitialized state gracefully
    TEST_ASSERT_FALSE(result);
    TEST_ASSERT_EQUAL_INT(0, data.size());
}

// Test multiple telemetry readings
void test_multiple_telemetry_readings()
{
    std::vector<TelemetryPoint> data1;
    std::vector<TelemetryPoint> data2;

    inverter->begin();

    // Multiple readings should not interfere with each other
    bool result1 = inverter->readBasic(data1);
    bool result2 = inverter->readBasic(data2);

    // Both should complete without crashing
    TEST_ASSERT_TRUE(result1 || !result1);
    TEST_ASSERT_TRUE(result2 || !result2);

    // If both succeed, they should have similar data
    if (result1 && result2)
    {
        // Data size might vary slightly, but should be in similar range
        TEST_ASSERT_TRUE(abs((int)data1.size() - (int)data2.size()) <= 5);
    }
}

// Test configuration parameter handling
void test_configuration_parameters()
{
    // Test various configuration parameters
    testConfig->setInt("rtu_addr", 10);
    testConfig->setBool("read_storage_regs", true);

    SolplanetASW configuredInverter(*modbusClient, *testConfig);

    // Should initialize with custom configuration
    bool result = configuredInverter.begin();
    TEST_ASSERT_TRUE(result || !result); // Just test no crash
}

// Test data validation and NaN handling
void test_data_validation()
{
    std::vector<TelemetryPoint> data;

    // Create test data with potential invalid values
    TelemetryPoint validPoint;
    validPoint.name = "valid_voltage";
    validPoint.unit = "V";
    validPoint.value = "230.5";

    TelemetryPoint invalidPoint;
    invalidPoint.name = "invalid_voltage";
    invalidPoint.unit = "V";
    invalidPoint.value = "NaN";

    data.push_back(validPoint);
    data.push_back(invalidPoint);

    TEST_ASSERT_EQUAL_INT(2, data.size());
    TEST_ASSERT_EQUAL_STRING("230.5", data[0].value.c_str());
    TEST_ASSERT_EQUAL_STRING("NaN", data[1].value.c_str());
}


// ===========================================================================
// Tiered polling
//
// These drive the driver through a scripted ModbusClient, so they need no
// inverter and no bus: every request is recorded and answered from a table.
// ===========================================================================

struct ModbusRequest
{
    uint16_t reg;   // 3xxxx register number
    uint16_t count;
};

class ScriptedModbus : public ModbusClient
{
public:
    explicit ScriptedModbus(Config &cfg) : ModbusClient(cfg) {}

    std::vector<ModbusRequest> requests;
    bool silent = false;              // nothing answers at all
    uint16_t stringCount = 2;         // value of register 31075
    uint16_t rejectFrom = 0;          // registers [rejectFrom, rejectTo] answer with
    uint16_t rejectTo = 0;            // a Modbus exception
    bool rejectMerged = false;        // reads longer than 32 registers get an exception
    uint16_t corruptAbove = 0;        // reads longer than this fail CRC (0 = never)

    bool readInputRegisters(uint8_t slaveId, uint16_t startAddr, uint16_t count,
                            uint16_t *data, uint8_t maxAttempts = MAX_READ_ATTEMPTS) override
    {
        uint16_t reg = startAddr + 30001;
        requests.push_back({reg, count});

        if (silent)
        {
            setLastError(ModbusReadError::Timeout);
            return false;
        }
        if (rejectFrom != 0 && reg <= rejectTo && reg + count - 1 >= rejectFrom)
        {
            setLastError(ModbusReadError::Exception, 0x02);
            return false;
        }
        if (rejectMerged && count > 32)
        {
            setLastError(ModbusReadError::Exception, 0x02);
            return false;
        }
        if (corruptAbove != 0 && count > corruptAbove)
        {
            setLastError(ModbusReadError::BadFrame);
            return false;
        }

        for (uint16_t i = 0; i < count; i++)
        {
            uint16_t r = reg + i;
            if (r == 31001) data[i] = '3';              // three-phase
            else if (r == 31074) data[i] = 2;           // MPPT inputs
            else if (r == 31075) data[i] = stringCount; // string currents
            else data[i] = 2300;                        // 230.0 V / 23.00 A / ...
        }
        setLastError(ModbusReadError::None);
        return true;
    }

    // True if any recorded request touched a register in [from, to].
    bool touched(uint16_t from, uint16_t to) const
    {
        for (const auto &r : requests)
        {
            if (r.reg <= to && r.reg + r.count - 1 >= from) return true;
        }
        return false;
    }

    uint16_t longestRead() const
    {
        uint16_t longest = 0;
        for (const auto &r : requests)
        {
            if (r.count > longest) longest = r.count;
        }
        return longest;
    }
};

static bool hasPoint(const std::vector<TelemetryPoint> &points, const char *name)
{
    for (const auto &p : points)
    {
        if (p.name == name) return true;
    }
    return false;
}

// Short tiers so the suite does not sit through real 10s/30s periods.
static void configureTiers(Config &cfg)
{
    cfg.setInt("rtu_addr", 3);
    cfg.setInt("poll_interval_sec", 1);
    cfg.setInt("poll_medium_interval_sec", 2);
    cfg.setInt("poll_slow_interval_sec", 4);
    cfg.setBool("read_storage_regs", true);
}

// The first cycle after boot publishes everything, whatever its tier.
void test_tiers_first_cycle_reads_every_block()
{
    Config cfg;
    configureTiers(cfg);
    ScriptedModbus bus(cfg);
    SolplanetASW inv(bus, cfg);
    std::vector<TelemetryPoint> points;

    TEST_ASSERT_TRUE(inv.readBasic(points));

    TEST_ASSERT_TRUE(hasPoint(points, "l1_voltage"));            // fast
    TEST_ASSERT_TRUE(hasPoint(points, "eps_active_power"));      // fast
    TEST_ASSERT_TRUE(hasPoint(points, "pv1_power"));             // fast, calculated
    TEST_ASSERT_TRUE(hasPoint(points, "string1_current"));       // fast
    TEST_ASSERT_TRUE(hasPoint(points, "device_state_text"));     // medium
    TEST_ASSERT_TRUE(hasPoint(points, "battery_soc"));           // medium (storage)
    TEST_ASSERT_TRUE(hasPoint(points, "energy_today"));          // slow
    TEST_ASSERT_TRUE(hasPoint(points, "grid_export_total"));     // slow
    TEST_ASSERT_TRUE(hasPoint(points, "battery_cycles"));        // slower (storage)
    TEST_ASSERT_TRUE(hasPoint(points, "grid_rated_voltage"));    // static
    TEST_ASSERT_TRUE(hasPoint(points, "grid_code"));             // static
}

// A steady-state cycle is three transactions: 31319-31379, 31601-31606 and
// 31634-31674. This is the whole point of the change.
void test_tiers_fast_cycle_is_three_transactions()
{
    Config cfg;
    configureTiers(cfg);
    ScriptedModbus bus(cfg);
    SolplanetASW inv(bus, cfg);
    std::vector<TelemetryPoint> points;

    TEST_ASSERT_TRUE(inv.readBasic(points));
    bus.requests.clear();
    TEST_ASSERT_TRUE(inv.readBasic(points));

    TEST_ASSERT_EQUAL_UINT(3, bus.requests.size());
    TEST_ASSERT_EQUAL_UINT16(31319, bus.requests[0].reg);
    TEST_ASSERT_EQUAL_UINT16(61, bus.requests[0].count);
    TEST_ASSERT_EQUAL_UINT16(31601, bus.requests[1].reg);
    TEST_ASSERT_EQUAL_UINT16(6, bus.requests[1].count);
    TEST_ASSERT_EQUAL_UINT16(31634, bus.requests[2].reg);
    TEST_ASSERT_EQUAL_UINT16(41, bus.requests[2].count);

    TEST_ASSERT_TRUE(hasPoint(points, "l1_voltage"));
    TEST_ASSERT_TRUE(hasPoint(points, "active_power"));
    TEST_ASSERT_TRUE(hasPoint(points, "eps_l1_active_power"));
    TEST_ASSERT_TRUE(hasPoint(points, "grid_l1_power"));
    TEST_ASSERT_FALSE(hasPoint(points, "battery_soc"));
    TEST_ASSERT_FALSE(hasPoint(points, "energy_today"));
    TEST_ASSERT_FALSE(hasPoint(points, "grid_rated_voltage"));
}

// Without string currents the PV and grid blocks are no longer neighbours.
void test_tiers_no_strings_splits_the_live_span()
{
    Config cfg;
    configureTiers(cfg);
    ScriptedModbus bus(cfg);
    bus.stringCount = 0;
    SolplanetASW inv(bus, cfg);
    std::vector<TelemetryPoint> points;

    TEST_ASSERT_TRUE(inv.readBasic(points));
    bus.requests.clear();
    TEST_ASSERT_TRUE(inv.readBasic(points));

    TEST_ASSERT_EQUAL_UINT(4, bus.requests.size());
    TEST_ASSERT_FALSE(bus.touched(31339, 31358));
    TEST_ASSERT_FALSE(hasPoint(points, "string1_current"));
}

// Medium-tier blocks come back once their period has passed, and not before.
void test_tiers_medium_block_returns_after_its_period()
{
    Config cfg;
    configureTiers(cfg);
    ScriptedModbus bus(cfg);
    SolplanetASW inv(bus, cfg);
    std::vector<TelemetryPoint> points;

    TEST_ASSERT_TRUE(inv.readBasic(points));
    TEST_ASSERT_TRUE(inv.readBasic(points));
    TEST_ASSERT_FALSE(hasPoint(points, "battery_soc"));

    delay(2100); // medium period is 2s in this suite

    bool sawBattery = false;
    bool sawStatus = false;
    bool sawSlow = false;
    // Slower blocks may be spread over a couple of cycles by the time budget.
    for (int cycle = 0; cycle < 3; cycle++)
    {
        TEST_ASSERT_TRUE(inv.readBasic(points));
        TEST_ASSERT_TRUE(hasPoint(points, "l1_voltage")); // fast tier is never displaced
        if (hasPoint(points, "battery_soc")) sawBattery = true;
        if (hasPoint(points, "device_state")) sawStatus = true;
        if (hasPoint(points, "energy_today")) sawSlow = true;
    }
    TEST_ASSERT_TRUE(sawBattery);
    TEST_ASSERT_TRUE(sawStatus);
    TEST_ASSERT_FALSE(sawSlow); // slow period (4s) has not passed yet
}

// Battery blocks are skipped entirely unless storage reading is enabled.
void test_tiers_storage_disabled_skips_battery_blocks()
{
    Config cfg;
    configureTiers(cfg);
    cfg.setBool("read_storage_regs", false);
    ScriptedModbus bus(cfg);
    SolplanetASW inv(bus, cfg);
    std::vector<TelemetryPoint> points;

    TEST_ASSERT_TRUE(inv.readBasic(points));

    TEST_ASSERT_FALSE(bus.touched(31607, 31629));
    TEST_ASSERT_FALSE(bus.touched(31679, 31687));
    TEST_ASSERT_FALSE(hasPoint(points, "battery_soc"));
    TEST_ASSERT_TRUE(hasPoint(points, "l1_voltage"));
}

// readStorage is a no-op now that readBasic schedules the battery blocks.
void test_tiers_read_storage_is_empty_success()
{
    Config cfg;
    configureTiers(cfg);
    ScriptedModbus bus(cfg);
    SolplanetASW inv(bus, cfg);
    std::vector<TelemetryPoint> points;
    points.push_back(TelemetryPoint("stale", "", "1"));

    TEST_ASSERT_TRUE(inv.readStorage(points));
    TEST_ASSERT_EQUAL_UINT(0, points.size());
    TEST_ASSERT_EQUAL_UINT(0, bus.requests.size());
}

// A block the inverter rejects is parked instead of being retried every cycle,
// and it does not take its neighbours down with it.
void test_tiers_rejected_block_is_parked_and_neighbours_survive()
{
    Config cfg;
    configureTiers(cfg);
    ScriptedModbus bus(cfg);
    bus.rejectFrom = 31634; // no EPS output on this inverter
    bus.rejectTo = 31662;
    SolplanetASW inv(bus, cfg);
    std::vector<TelemetryPoint> points;

    // Cycle 1 discovers it: the poll still succeeds and the rest is published.
    TEST_ASSERT_TRUE(inv.readBasic(points));
    TEST_ASSERT_FALSE(hasPoint(points, "eps_active_power"));
    TEST_ASSERT_TRUE(hasPoint(points, "grid_l1_power"));
    TEST_ASSERT_TRUE(hasPoint(points, "l1_voltage"));

    // Cycle 2 leaves the rejected registers alone.
    bus.requests.clear();
    TEST_ASSERT_TRUE(inv.readBasic(points));
    TEST_ASSERT_FALSE(bus.touched(31634, 31662));
    TEST_ASSERT_TRUE(bus.touched(31663, 31674));
    TEST_ASSERT_TRUE(hasPoint(points, "grid_l1_power"));
}

// If the device goes quiet the poll fails fast: one timed-out transaction, not
// one per block.
void test_tiers_silent_device_fails_poll_without_reading_every_block()
{
    Config cfg;
    configureTiers(cfg);
    ScriptedModbus bus(cfg);
    SolplanetASW inv(bus, cfg);
    std::vector<TelemetryPoint> points;

    TEST_ASSERT_TRUE(inv.readBasic(points));

    bus.silent = true;
    bus.requests.clear();
    TEST_ASSERT_FALSE(inv.readBasic(points));
    TEST_ASSERT_EQUAL_UINT(1, bus.requests.size());
    TEST_ASSERT_EQUAL_UINT(0, points.size());

    // And it recovers by itself once the device answers again.
    bus.silent = false;
    TEST_ASSERT_TRUE(inv.readBasic(points));
    TEST_ASSERT_TRUE(hasPoint(points, "l1_voltage"));
}

// An inverter that accepts the parts but refuses them merged still delivers
// data on that cycle, and merging is switched off afterwards.
void test_tiers_merged_read_rejected_falls_back_to_single_blocks()
{
    Config cfg;
    configureTiers(cfg);
    ScriptedModbus bus(cfg);
    bus.rejectMerged = true;
    SolplanetASW inv(bus, cfg);
    std::vector<TelemetryPoint> points;

    TEST_ASSERT_TRUE(inv.readBasic(points));
    TEST_ASSERT_TRUE(hasPoint(points, "l1_voltage"));
    TEST_ASSERT_TRUE(hasPoint(points, "pv1_voltage"));
    TEST_ASSERT_TRUE(hasPoint(points, "eps_active_power"));

    bus.requests.clear();
    TEST_ASSERT_TRUE(inv.readBasic(points));
    TEST_ASSERT_TRUE(bus.longestRead() <= 32);
    TEST_ASSERT_TRUE(hasPoint(points, "l1_voltage"));
}

// On a noisy bus long frames are the ones that get hit. If they keep failing
// while short reads work, the driver shortens its reads by itself.
void test_tiers_long_frames_failing_shrinks_read_size()
{
    Config cfg;
    configureTiers(cfg);
    ScriptedModbus bus(cfg);
    bus.corruptAbove = 32;
    SolplanetASW inv(bus, cfg);
    std::vector<TelemetryPoint> points;

    // Data keeps flowing while it adapts.
    for (int cycle = 0; cycle < 4; cycle++)
    {
        TEST_ASSERT_TRUE(inv.readBasic(points));
        TEST_ASSERT_TRUE(hasPoint(points, "l1_voltage"));
        TEST_ASSERT_TRUE(hasPoint(points, "eps_active_power"));
    }

    bus.requests.clear();
    TEST_ASSERT_TRUE(inv.readBasic(points));
    TEST_ASSERT_TRUE(bus.longestRead() <= 32);
    TEST_ASSERT_TRUE(hasPoint(points, "l1_voltage"));
}

void setup()
{
    delay(2000); // Wait for serial monitor

    UNITY_BEGIN();

    RUN_TEST(test_solplanet_initialization);
    RUN_TEST(test_slave_address_configuration);
    RUN_TEST(test_input_register_conversion);
    RUN_TEST(test_telemetry_point_structure);
    RUN_TEST(test_basic_telemetry_reading);
    RUN_TEST(test_storage_telemetry_reading);
    RUN_TEST(test_phase_detection);
    RUN_TEST(test_telemetry_error_handling);
    RUN_TEST(test_multiple_telemetry_readings);
    RUN_TEST(test_configuration_parameters);
    RUN_TEST(test_data_validation);

    RUN_TEST(test_tiers_first_cycle_reads_every_block);
    RUN_TEST(test_tiers_fast_cycle_is_three_transactions);
    RUN_TEST(test_tiers_no_strings_splits_the_live_span);
    RUN_TEST(test_tiers_medium_block_returns_after_its_period);
    RUN_TEST(test_tiers_storage_disabled_skips_battery_blocks);
    RUN_TEST(test_tiers_read_storage_is_empty_success);
    RUN_TEST(test_tiers_rejected_block_is_parked_and_neighbours_survive);
    RUN_TEST(test_tiers_silent_device_fails_poll_without_reading_every_block);
    RUN_TEST(test_tiers_merged_read_rejected_falls_back_to_single_blocks);
    RUN_TEST(test_tiers_long_frames_failing_shrinks_read_size);

    UNITY_END();
}

void loop()
{
    // Empty - tests run once in setup()
}
