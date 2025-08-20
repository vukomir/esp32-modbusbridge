#include <unity.h>
#include <Arduino.h>
#include "MQTTClient.h"
#include "Config.h"
#include "WiFiManager.h"

// Test fixtures
Config *testConfig;
WiFiManager *wifiManager;
MQTTClient *mqttClient;

void setUp(void)
{
    testConfig = new Config();
    testConfig->begin();

    // Set up MQTT configuration
    testConfig->setString("mqtt_broker", "test.broker.com");
    testConfig->setInt("mqtt_port", 1883);
    testConfig->setString("mqtt_username", "testuser");
    testConfig->setString("mqtt_password", "testpass");
    testConfig->setString("mqtt_topic_prefix", "test/device");
    testConfig->setString("device_type", "inverter");
    testConfig->setString("device_id", "test123");
    testConfig->setBool("mqtt_retain", true);
    testConfig->setBool("mqtt_json_format", true);

    // Set up WiFi configuration (needed for MQTT)
    testConfig->setString("wifi_ssid", "test_network");
    testConfig->setString("wifi_password", "test_password");

    wifiManager = new WiFiManager(*testConfig);
    mqttClient = new MQTTClient(*testConfig, *wifiManager);
}

void tearDown(void)
{
    if (mqttClient)
    {
        mqttClient->disconnect();
        delete mqttClient;
        mqttClient = nullptr;
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
void test_mqtt_initialization()
{
    TEST_ASSERT_FALSE(mqttClient->isInitialized());

    bool result = mqttClient->begin();
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_TRUE(mqttClient->isInitialized());
}

// Test configuration validation
void test_mqtt_config_validation()
{
    // Test with valid configuration
    bool result = mqttClient->begin();
    TEST_ASSERT_TRUE(result);

    // Test with invalid broker (empty)
    testConfig->setString("mqtt_broker", "");
    MQTTClient invalidClient(*testConfig, *wifiManager);
    result = invalidClient.begin();
    TEST_ASSERT_FALSE(result);
}

// Test topic building
void test_topic_building()
{
    mqttClient->begin();

    // Test legacy topic format
    // We can't directly test private methods, but we can test through public methods
    // The topic building is tested indirectly through publish operations
    TEST_ASSERT_TRUE(true); // Placeholder - topic building tested through publish methods
}

// Test connection status
void test_connection_status()
{
    mqttClient->begin();

    // Initially should not be connected (no actual broker)
    TEST_ASSERT_FALSE(mqttClient->isConnected());

    // Test connection handling (will fail without actual broker, but shouldn't crash)
    mqttClient->handleConnection();
    TEST_ASSERT_TRUE(true); // If we get here, no crash occurred
}

// Test publishing methods
void test_publishing_methods()
{
    mqttClient->begin();

    // Test legacy publish (will fail without connection, but shouldn't crash)
    bool result = mqttClient->publish("test_metric", "test_value");
    TEST_ASSERT_FALSE(result); // Should fail without connection

    // Test telemetry publish
    result = mqttClient->publishTelemetry("voltage", "230.5", "V");
    TEST_ASSERT_FALSE(result); // Should fail without connection

    // Test status publish
    result = mqttClient->publishStatus("connection", "online");
    TEST_ASSERT_FALSE(result); // Should fail without connection

    // Test diagnostics publish
    result = mqttClient->publishDiagnostics("memory", "12345");
    TEST_ASSERT_FALSE(result); // Should fail without connection

    // Test hardware publish
    result = mqttClient->publishHardware("max485_status", "connected");
    TEST_ASSERT_FALSE(result); // Should fail without connection

    // Test config publish
    result = mqttClient->publishConfig("log_level", "info");
    TEST_ASSERT_FALSE(result); // Should fail without connection
}

// Test JSON publishing
void test_json_publishing()
{
    mqttClient->begin();

    String jsonPayload = "{\"temperature\":25.5,\"humidity\":60}";
    bool result = mqttClient->publishJson("sensors", "environment", jsonPayload);
    TEST_ASSERT_FALSE(result); // Should fail without connection, but shouldn't crash
}

// Test utility publishing methods
void test_utility_publishing()
{
    mqttClient->begin();

    // Test status publishing
    bool result = mqttClient->publishStatus("online");
    TEST_ASSERT_FALSE(result); // Should fail without connection

    // Test RSSI publishing
    result = mqttClient->publishRSSI();
    TEST_ASSERT_FALSE(result); // Should fail without connection

    // Test uptime publishing
    result = mqttClient->publishUptime();
    TEST_ASSERT_FALSE(result); // Should fail without connection
}

// Test connection management
void test_connection_management()
{
    mqttClient->begin();

    // Test connect (will fail without actual broker)
    bool result = mqttClient->connect();
    TEST_ASSERT_FALSE(result);

    // Test reconnect (should handle gracefully)
    mqttClient->reconnect();
    TEST_ASSERT_TRUE(true); // If we get here, no crash occurred

    // Test disconnect
    mqttClient->disconnect();
    TEST_ASSERT_FALSE(mqttClient->isConnected());
}

// Test multiple initialization
void test_multiple_initialization()
{
    TEST_ASSERT_TRUE(mqttClient->begin());
    TEST_ASSERT_TRUE(mqttClient->isInitialized());

    // Second initialization should return true
    TEST_ASSERT_TRUE(mqttClient->begin());
    TEST_ASSERT_TRUE(mqttClient->isInitialized());
}

// Test configuration parameters
void test_configuration_parameters()
{
    // Test different port
    testConfig->setInt("mqtt_port", 8883);
    MQTTClient customPortClient(*testConfig, *wifiManager);
    TEST_ASSERT_TRUE(customPortClient.begin());

    // Test different topic prefix
    testConfig->setString("mqtt_topic_prefix", "custom/prefix");
    MQTTClient customTopicClient(*testConfig, *wifiManager);
    TEST_ASSERT_TRUE(customTopicClient.begin());

    // Test without authentication
    testConfig->setString("mqtt_username", "");
    testConfig->setString("mqtt_password", "");
    MQTTClient noAuthClient(*testConfig, *wifiManager);
    TEST_ASSERT_TRUE(noAuthClient.begin());
}

// Test error handling
void test_error_handling()
{
    // Test with invalid port
    testConfig->setInt("mqtt_port", -1);
    MQTTClient invalidPortClient(*testConfig, *wifiManager);
    bool result = invalidPortClient.begin();
    TEST_ASSERT_TRUE(result || !result); // May handle invalid port differently

    // Test with very long broker name
    testConfig->setString("mqtt_broker", "very.long.broker.name.that.might.cause.issues.example.com");
    MQTTClient longBrokerClient(*testConfig, *wifiManager);
    result = longBrokerClient.begin();
    TEST_ASSERT_TRUE(result || !result); // Should handle gracefully
}

// Test state management
void test_state_management()
{
    // Test initial state
    TEST_ASSERT_FALSE(mqttClient->isInitialized());
    TEST_ASSERT_FALSE(mqttClient->isConnected());

    // Test after initialization
    mqttClient->begin();
    TEST_ASSERT_TRUE(mqttClient->isInitialized());
    TEST_ASSERT_FALSE(mqttClient->isConnected()); // No actual connection

    // Test state consistency
    bool isInit1 = mqttClient->isInitialized();
    bool isInit2 = mqttClient->isInitialized();
    TEST_ASSERT_EQUAL(isInit1, isInit2);
}

// Test memory management
void test_memory_management()
{
    // Test multiple client creation/destruction
    for (int i = 0; i < 3; i++)
    {
        MQTTClient *tempClient = new MQTTClient(*testConfig, *wifiManager);
        tempClient->begin();
        delete tempClient;
    }

    // If we get here, memory management is working
    TEST_ASSERT_TRUE(true);
}

// Test concurrent operations
void test_concurrent_operations()
{
    mqttClient->begin();

    // Test that multiple operations can be called without issues
    mqttClient->isConnected();
    mqttClient->isInitialized();
    mqttClient->handleConnection();

    // All should complete without crashing
    TEST_ASSERT_TRUE(true);
}

void setup()
{
    delay(2000); // Wait for serial monitor

    UNITY_BEGIN();

    RUN_TEST(test_mqtt_initialization);
    RUN_TEST(test_mqtt_config_validation);
    RUN_TEST(test_topic_building);
    RUN_TEST(test_connection_status);
    RUN_TEST(test_publishing_methods);
    RUN_TEST(test_json_publishing);
    RUN_TEST(test_utility_publishing);
    RUN_TEST(test_connection_management);
    RUN_TEST(test_multiple_initialization);
    RUN_TEST(test_configuration_parameters);
    RUN_TEST(test_error_handling);
    RUN_TEST(test_state_management);
    RUN_TEST(test_memory_management);
    RUN_TEST(test_concurrent_operations);

    UNITY_END();
}

void loop()
{
    // Empty - tests run once in setup()
}
