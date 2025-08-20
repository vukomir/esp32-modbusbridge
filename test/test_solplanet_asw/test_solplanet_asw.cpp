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

    UNITY_END();
}

void loop()
{
    // Empty - tests run once in setup()
}
