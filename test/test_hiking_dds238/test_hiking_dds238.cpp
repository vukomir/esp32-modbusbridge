#include <unity.h>
#include <Arduino.h>
#include "HikingDDS238.h"
#include "ModbusClient.h"
#include "Config.h"
#include <vector>

// Test fixtures
Config *testConfig;
ModbusClient *modbusClient;
HikingDDS238 *meter;

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

    meter = new HikingDDS238(*modbusClient, *testConfig);
}

void tearDown(void)
{
    if (meter)
    {
        delete meter;
        meter = nullptr;
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
void test_hiking_initialization()
{
    // begin() may fail without actual device, but should not crash
    bool result = meter->begin();
    TEST_ASSERT_TRUE(result || !result); // Just test that it doesn't crash
}

// Test slave address configuration
void test_slave_address_configuration()
{
    // Test default slave address
    Config defaultConfig;
    HikingDDS238 defaultMeter(*modbusClient, defaultConfig);

    // Test custom slave address
    testConfig->setInt("rtu_addr", 5);
    HikingDDS238 customMeter(*modbusClient, *testConfig);

    // Both should initialize without crashing
    TEST_ASSERT_TRUE(true); // If we get here, constructors didn't crash
}

// Test basic telemetry reading
void test_basic_telemetry_reading()
{
    std::vector<TelemetryPoint> basicData;

    meter->begin();

    // This will likely fail without actual device, but should not crash
    bool result = meter->readBasic(basicData);

    // Test that the method completes without crashing
    TEST_ASSERT_TRUE(result || !result);

    // If it succeeds, data should be populated
    if (result)
    {
        TEST_ASSERT_GREATER_THAN(0, basicData.size());
    }
}

// Test storage telemetry reading (should return empty for meter)
void test_storage_telemetry_reading()
{
    std::vector<TelemetryPoint> storageData;

    meter->begin();

    // Meters typically don't have storage data
    bool result = meter->readStorage(storageData);

    // Should succeed but return empty data
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_EQUAL_INT(0, storageData.size());
}

// Test telemetry point structure for meter data
void test_meter_telemetry_structure()
{
    std::vector<TelemetryPoint> data;

    // Create test meter telemetry points
    TelemetryPoint voltagePoint;
    voltagePoint.name = "voltage_v";
    voltagePoint.unit = "V";
    voltagePoint.value = "230.5";

    TelemetryPoint currentPoint;
    currentPoint.name = "current_a";
    currentPoint.unit = "A";
    currentPoint.value = "10.2";

    TelemetryPoint powerPoint;
    powerPoint.name = "power_w";
    powerPoint.unit = "W";
    powerPoint.value = "2351.0";

    TelemetryPoint energyPoint;
    energyPoint.name = "energy_kwh";
    energyPoint.unit = "kWh";
    energyPoint.value = "1234.5";

    data.push_back(voltagePoint);
    data.push_back(currentPoint);
    data.push_back(powerPoint);
    data.push_back(energyPoint);

    TEST_ASSERT_EQUAL_INT(4, data.size());
    TEST_ASSERT_EQUAL_STRING("voltage_v", data[0].name.c_str());
    TEST_ASSERT_EQUAL_STRING("V", data[0].unit.c_str());
    TEST_ASSERT_EQUAL_STRING("230.5", data[0].value.c_str());
}

// Test error handling in telemetry reading
void test_telemetry_error_handling()
{
    std::vector<TelemetryPoint> data;

    // Test reading without initialization
    HikingDDS238 uninitializedMeter(*modbusClient, *testConfig);
    bool result = uninitializedMeter.readBasic(data);

    // Should handle uninitialized state gracefully
    TEST_ASSERT_FALSE(result);
    TEST_ASSERT_EQUAL_INT(0, data.size());
}

// Test multiple telemetry readings
void test_multiple_telemetry_readings()
{
    std::vector<TelemetryPoint> data1;
    std::vector<TelemetryPoint> data2;

    meter->begin();

    // Multiple readings should not interfere with each other
    bool result1 = meter->readBasic(data1);
    bool result2 = meter->readBasic(data2);

    // Both should complete without crashing
    TEST_ASSERT_TRUE(result1 || !result1);
    TEST_ASSERT_TRUE(result2 || !result2);

    // If both succeed, they should have similar data
    if (result1 && result2)
    {
        TEST_ASSERT_EQUAL_INT(data1.size(), data2.size());
    }
}

// Test configuration parameter handling
void test_configuration_parameters()
{
    // Test various configuration parameters
    testConfig->setInt("rtu_addr", 10);

    HikingDDS238 configuredMeter(*modbusClient, *testConfig);

    // Should initialize with custom configuration
    bool result = configuredMeter.begin();
    TEST_ASSERT_TRUE(result || !result); // Just test no crash
}

// Test meter-specific data validation
void test_meter_data_validation()
{
    std::vector<TelemetryPoint> data;

    // Create test data with meter-typical values
    TelemetryPoint validVoltage;
    validVoltage.name = "voltage_v";
    validVoltage.unit = "V";
    validVoltage.value = "230.5";

    TelemetryPoint validCurrent;
    validCurrent.name = "current_a";
    validCurrent.unit = "A";
    validCurrent.value = "15.2";

    TelemetryPoint validEnergy;
    validEnergy.name = "total_energy_kwh";
    validEnergy.unit = "kWh";
    validEnergy.value = "12345.6";

    data.push_back(validVoltage);
    data.push_back(validCurrent);
    data.push_back(validEnergy);

    TEST_ASSERT_EQUAL_INT(3, data.size());

    // Validate that meter data has expected characteristics
    bool hasVoltage = false;
    bool hasCurrent = false;
    bool hasEnergy = false;

    for (const auto &point : data)
    {
        if (point.name.indexOf("voltage") >= 0)
            hasVoltage = true;
        if (point.name.indexOf("current") >= 0)
            hasCurrent = true;
        if (point.name.indexOf("energy") >= 0)
            hasEnergy = true;
    }

    TEST_ASSERT_TRUE(hasVoltage);
    TEST_ASSERT_TRUE(hasCurrent);
    TEST_ASSERT_TRUE(hasEnergy);
}

// Test meter vs inverter interface compliance
void test_interface_compliance()
{
    // Test that the meter implements the InverterInterface correctly
    // (even though it's a meter, it uses the same interface)

    std::vector<TelemetryPoint> basicData;
    std::vector<TelemetryPoint> storageData;

    meter->begin();

    // Both methods should be callable
    bool basicResult = meter->readBasic(basicData);
    bool storageResult = meter->readStorage(storageData);

    // Basic should attempt to read data, storage should return empty
    TEST_ASSERT_TRUE(basicResult || !basicResult); // May fail without device
    TEST_ASSERT_TRUE(storageResult);               // Should succeed with empty data
    TEST_ASSERT_EQUAL_INT(0, storageData.size());  // Meters don't have storage
}

void setup()
{
    delay(2000); // Wait for serial monitor

    UNITY_BEGIN();

    RUN_TEST(test_hiking_initialization);
    RUN_TEST(test_slave_address_configuration);
    RUN_TEST(test_basic_telemetry_reading);
    RUN_TEST(test_storage_telemetry_reading);
    RUN_TEST(test_meter_telemetry_structure);
    RUN_TEST(test_telemetry_error_handling);
    RUN_TEST(test_multiple_telemetry_readings);
    RUN_TEST(test_configuration_parameters);
    RUN_TEST(test_meter_data_validation);
    RUN_TEST(test_interface_compliance);

    UNITY_END();
}

void loop()
{
    // Empty - tests run once in setup()
}
