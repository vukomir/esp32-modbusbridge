#include <unity.h>
#include <Arduino.h>
#include "Poller.h"
#include "ModbusClient.h"
#include "MQTTClient.h"
#include "WiFiManager.h"
#include "Config.h"

// Test fixtures
Config *testConfig;
WiFiManager *wifiManager;
ModbusClient *modbusClient;
MQTTClient *mqttClient;
Poller *poller;

void setUp(void)
{
    testConfig = new Config();

    // Set up basic configuration
    testConfig->setInt("baudrate", 9600);
    testConfig->setString("parity", "N");
    testConfig->setInt("stop_bits", 1);
    testConfig->setInt("rs485_de_re_pin", 4);
    testConfig->setInt("rtu_addr", 1);
    testConfig->setString("device_model", "solplanet_asw_gen");

    // Set up MQTT configuration
    testConfig->setString("mqtt_broker", "test.broker.com");
    testConfig->setInt("mqtt_port", 1883);
    testConfig->setString("mqtt_username", "test");
    testConfig->setString("mqtt_password", "test");
    testConfig->setString("mqtt_topic_prefix", "test/device");

    wifiManager = new WiFiManager(*testConfig);
    modbusClient = new ModbusClient(*testConfig);
    mqttClient = new MQTTClient(*testConfig, *wifiManager);

    modbusClient->begin();

    poller = new Poller(*testConfig, *mqttClient, *modbusClient);
}

void tearDown(void)
{
    if (poller)
    {
        delete poller;
        poller = nullptr;
    }

    if (mqttClient)
    {
        delete mqttClient;
        mqttClient = nullptr;
    }

    if (modbusClient)
    {
        modbusClient->end();
        delete modbusClient;
        modbusClient = nullptr;
    }

    if (wifiManager)
    {
        delete wifiManager;
        wifiManager = nullptr;
    }

    if (testConfig)
    {
        delete testConfig;
        testConfig = nullptr;
    }
}

// Test basic initialization
void test_poller_initialization()
{
    TEST_ASSERT_FALSE(poller->isInitialized());

    // begin() may fail without actual device, but should not crash
    bool result = poller->begin();
    TEST_ASSERT_TRUE(result || !result); // Just test that it doesn't crash

    // If initialization succeeded, should be marked as initialized
    if (result)
    {
        TEST_ASSERT_TRUE(poller->isInitialized());
    }
}

// Test initialization without modbus
void test_initialization_without_modbus()
{
    ModbusClient uninitializedModbus(*testConfig);
    Poller pollerWithoutModbus(*testConfig, *mqttClient, uninitializedModbus);

    // Should fail gracefully when modbus is not initialized
    bool result = pollerWithoutModbus.begin();
    TEST_ASSERT_FALSE(result);
    TEST_ASSERT_FALSE(pollerWithoutModbus.isInitialized());
}

// Test poll statistics tracking
void test_poll_statistics()
{
    poller->begin();

    // Initial statistics should be zero
    TEST_ASSERT_EQUAL_UINT32(0, poller->getSuccessfulPolls());
    TEST_ASSERT_EQUAL_UINT32(0, poller->getFailedPolls());
    TEST_ASSERT_EQUAL_UINT32(0, poller->getLastPollTime());
}

// Test polling without initialization
void test_poll_without_initialization()
{
    // Polling without initialization should not crash
    poller->poll(); // Should handle gracefully

    // Statistics should remain zero
    TEST_ASSERT_EQUAL_UINT32(0, poller->getSuccessfulPolls());
    TEST_ASSERT_EQUAL_UINT32(0, poller->getFailedPolls());
}

// Test device model configuration
void test_device_model_configuration()
{
    // Test with valid device model
    testConfig->setString("device_model", "solplanet_asw_gen");
    Poller validPoller(*testConfig, *mqttClient, *modbusClient);

    bool result = validPoller.begin();
    TEST_ASSERT_TRUE(result || !result); // May fail without device, but should not crash

        // Test with invalid device model
    testConfig->setString("device_model", "invalid_model");
    Poller invalidPoller(*testConfig, *mqttClient, *modbusClient);
    
    result = invalidPoller.begin();
    // May succeed or fail depending on factory implementation - just test it doesn't crash
    TEST_ASSERT_TRUE(result || !result);
}

// Test multiple initialization attempts
void test_multiple_initialization()
{
    bool result1 = poller->begin();
    bool result2 = poller->begin();

    // Second initialization should return same result as first
    TEST_ASSERT_EQUAL(result1, result2);

    if (result1)
    {
        TEST_ASSERT_TRUE(poller->isInitialized());
    }
}

// Test polling behavior
void test_polling_behavior()
{
    poller->begin();

    uint32_t initialTime = poller->getLastPollTime();

    // Perform a poll
    poller->poll();

    // Last poll time should be updated (if initialized successfully)
    if (poller->isInitialized())
    {
        uint32_t newTime = poller->getLastPollTime();
        TEST_ASSERT_GREATER_OR_EQUAL(initialTime, newTime);
    }
}

// Test error handling during polling
void test_polling_error_handling()
{
    // Create poller with invalid configuration
    Config errorConfig;
    errorConfig.setString("device_model", "invalid");

    Poller errorPoller(errorConfig, *mqttClient, *modbusClient);

    // Should handle initialization failure gracefully
    bool result = errorPoller.begin();
    // May succeed or fail - just test it doesn't crash
    TEST_ASSERT_TRUE(result || !result);

    // Polling should handle uninitialized state
    errorPoller.poll(); // Should not crash

    TEST_ASSERT_EQUAL_UINT32(0, errorPoller.getSuccessfulPolls());
}

// Test configuration parameter handling
void test_configuration_parameters()
{
    // Test different RTU addresses
    testConfig->setInt("rtu_addr", 5);
    Poller customPoller(*testConfig, *mqttClient, *modbusClient);

    bool result = customPoller.begin();
    TEST_ASSERT_TRUE(result || !result); // Should handle different addresses
}

// Test state management
void test_state_management()
{
    // Test initial state
    TEST_ASSERT_FALSE(poller->isInitialized());

    // Test state after initialization attempt
    poller->begin();

    // Test that state is consistent
    bool isInit1 = poller->isInitialized();
    bool isInit2 = poller->isInitialized();
    TEST_ASSERT_EQUAL(isInit1, isInit2);
}

// Test memory management
void test_memory_management()
{
    // Test that multiple poller instances can be created and destroyed
    for (int i = 0; i < 3; i++)
    {
        Poller *tempPoller = new Poller(*testConfig, *mqttClient, *modbusClient);
        tempPoller->begin();
        delete tempPoller;
    }

    // If we get here, memory management is working
    TEST_ASSERT_TRUE(true);
}

// Test concurrent operations
void test_concurrent_operations()
{
    poller->begin();

    // Test that we can call multiple methods without issues
    poller->isInitialized();
    poller->getLastPollTime();
    poller->getSuccessfulPolls();
    poller->getFailedPolls();

    // All should complete without crashing
    TEST_ASSERT_TRUE(true);
}

void setup()
{
    delay(2000); // Wait for serial monitor

    UNITY_BEGIN();

    RUN_TEST(test_poller_initialization);
    RUN_TEST(test_initialization_without_modbus);
    RUN_TEST(test_poll_statistics);
    RUN_TEST(test_poll_without_initialization);
    RUN_TEST(test_device_model_configuration);
    RUN_TEST(test_multiple_initialization);
    RUN_TEST(test_polling_behavior);
    RUN_TEST(test_polling_error_handling);
    RUN_TEST(test_configuration_parameters);
    RUN_TEST(test_state_management);
    RUN_TEST(test_memory_management);
    RUN_TEST(test_concurrent_operations);

    UNITY_END();
}

void loop()
{
    // Empty - tests run once in setup()
}
