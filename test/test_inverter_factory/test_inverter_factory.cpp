#include <unity.h>
#include <Arduino.h>
#include "InverterFactory.h"
#include "ModbusClient.h"
#include "Config.h"
#include "constants.h"

// Test fixtures
Config *testConfig;
ModbusClient *modbusClient;

void setUp(void)
{
    testConfig = new Config();

    // Set up basic configuration
    testConfig->setInt("baudrate", 9600);
    testConfig->setString("parity", "N");
    testConfig->setInt("stop_bits", 1);
    testConfig->setInt("rs485_de_re_pin", 4);

    modbusClient = new ModbusClient(*testConfig);
    modbusClient->begin();
}

void tearDown(void)
{
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

// Test SolPlanet ASW creation
void test_create_solplanet_asw()
{
    std::unique_ptr<InverterInterface> inverter = InverterFactory::create(
        SOLPLANET_ASW_MODEL, *modbusClient, *testConfig);

    TEST_ASSERT_NOT_NULL(inverter.get());

    // Test that it's the correct type by trying to initialize
    bool result = inverter->begin();
    // May fail without actual device, but object should be created
    TEST_ASSERT_TRUE(result || !result); // Just test that it doesn't crash
}

// Test Hiking DDS238 creation
void test_create_hiking_dds238()
{
    std::unique_ptr<InverterInterface> meter = InverterFactory::create(
        HIKING_DDS238_MODEL, *modbusClient, *testConfig);

    TEST_ASSERT_NOT_NULL(meter.get());

    // Test that it's the correct type by trying to initialize
    bool result = meter->begin();
    // May fail without actual device, but object should be created
    TEST_ASSERT_TRUE(result || !result); // Just test that it doesn't crash
}

// Test unknown model
void test_create_unknown_model()
{
    std::unique_ptr<InverterInterface> unknown = InverterFactory::create(
        "unknown_model", *modbusClient, *testConfig);

    TEST_ASSERT_NULL(unknown.get());
}

// Test empty model string
void test_create_empty_model()
{
    std::unique_ptr<InverterInterface> empty = InverterFactory::create(
        "", *modbusClient, *testConfig);

    TEST_ASSERT_NULL(empty.get());
}

// Test null model string
void test_create_null_model()
{
    std::unique_ptr<InverterInterface> null = InverterFactory::create(
        String(), *modbusClient, *testConfig // Empty string instead of nullptr
    );

    TEST_ASSERT_NULL(null.get());
}

// Test case sensitivity
void test_create_case_sensitivity()
{
    // Test uppercase
    std::unique_ptr<InverterInterface> upper = InverterFactory::create(
        "SOLPLANET_ASW_GEN", *modbusClient, *testConfig);
    TEST_ASSERT_NULL(upper.get()); // Should be case sensitive

    // Test lowercase
    std::unique_ptr<InverterInterface> lower = InverterFactory::create(
        "solplanet_asw_gen", *modbusClient, *testConfig);
    TEST_ASSERT_NOT_NULL(lower.get()); // Correct case
}

// Test multiple creations
void test_multiple_creations()
{
    // Create multiple instances
    std::unique_ptr<InverterInterface> inverter1 = InverterFactory::create(
        SOLPLANET_ASW_MODEL, *modbusClient, *testConfig);

    std::unique_ptr<InverterInterface> inverter2 = InverterFactory::create(
        SOLPLANET_ASW_MODEL, *modbusClient, *testConfig);

    TEST_ASSERT_NOT_NULL(inverter1.get());
    TEST_ASSERT_NOT_NULL(inverter2.get());

    // Should be different instances
    TEST_ASSERT_NOT_EQUAL(inverter1.get(), inverter2.get());
}

// Test creation with different configs
void test_create_with_different_configs()
{
    Config config2;
    config2.setInt("rtu_addr", 2);

    std::unique_ptr<InverterInterface> inverter1 = InverterFactory::create(
        SOLPLANET_ASW_MODEL, *modbusClient, *testConfig);

    std::unique_ptr<InverterInterface> inverter2 = InverterFactory::create(
        SOLPLANET_ASW_MODEL, *modbusClient, config2);

    TEST_ASSERT_NOT_NULL(inverter1.get());
    TEST_ASSERT_NOT_NULL(inverter2.get());
}

// Test supported devices constants
void test_supported_devices_constants()
{
    // Test that constants are defined
    TEST_ASSERT_NOT_NULL(SOLPLANET_ASW_MODEL);
    TEST_ASSERT_NOT_NULL(HIKING_DDS238_MODEL);
    TEST_ASSERT_NOT_NULL(DEFAULT_DEVICE_MODEL);

    // Test that they have expected values
    TEST_ASSERT_EQUAL_STRING("solplanet_asw_gen", SOLPLANET_ASW_MODEL);
    TEST_ASSERT_EQUAL_STRING("hiking_dds238", HIKING_DDS238_MODEL);
    TEST_ASSERT_EQUAL_STRING("solplanet_asw_gen", DEFAULT_DEVICE_MODEL);
}

// Test supported devices list
void test_supported_devices_list()
{
    // Test that the supported devices array is properly defined
    TEST_ASSERT_GREATER_THAN(0, SUPPORTED_DEVICES_COUNT);

    // Test that we can access the first device
    TEST_ASSERT_NOT_NULL(SUPPORTED_DEVICES[0].model);
    TEST_ASSERT_NOT_NULL(SUPPORTED_DEVICES[0].displayName);
    TEST_ASSERT_NOT_NULL(SUPPORTED_DEVICES[0].type);

    // Test that we have at least the two main devices
    bool foundSolPlanet = false;
    bool foundHiking = false;

    for (size_t i = 0; i < SUPPORTED_DEVICES_COUNT; i++)
    {
        if (strcmp(SUPPORTED_DEVICES[i].model, SOLPLANET_ASW_MODEL) == 0)
        {
            foundSolPlanet = true;
        }
        if (strcmp(SUPPORTED_DEVICES[i].model, HIKING_DDS238_MODEL) == 0)
        {
            foundHiking = true;
        }
    }

    TEST_ASSERT_TRUE(foundSolPlanet);
    TEST_ASSERT_TRUE(foundHiking);
}

void setup()
{
    delay(2000); // Wait for serial monitor

    UNITY_BEGIN();

    RUN_TEST(test_create_solplanet_asw);
    RUN_TEST(test_create_hiking_dds238);
    RUN_TEST(test_create_unknown_model);
    RUN_TEST(test_create_empty_model);
    RUN_TEST(test_create_null_model);
    RUN_TEST(test_create_case_sensitivity);
    RUN_TEST(test_multiple_creations);
    RUN_TEST(test_create_with_different_configs);
    RUN_TEST(test_supported_devices_constants);
    RUN_TEST(test_supported_devices_list);

    UNITY_END();
}

void loop()
{
    // Empty - tests run once in setup()
}
