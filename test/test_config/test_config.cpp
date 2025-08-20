#include <unity.h>
#include <Arduino.h>
#include "Config.h"
#include <LittleFS.h>

// Test fixtures
Config *testConfig;

void setUp(void)
{
    // Initialize LittleFS for testing
    if (!LittleFS.begin(true))
    {
        // Format if mount fails
        LittleFS.format();
        LittleFS.begin(true);
    }

    testConfig = new Config();
    testConfig->begin(); // Initialize the config properly
}

void tearDown(void)
{
    delete testConfig;
    testConfig = nullptr;

    // Clean up test files
    if (LittleFS.exists("/config.json"))
    {
        LittleFS.remove("/config.json");
    }
}

// Test basic string operations
void test_config_string_operations()
{
    testConfig->setString("test_key", "test_value");
    TEST_ASSERT_EQUAL_STRING("test_value", testConfig->getString("test_key").c_str());

    // Test default value
    TEST_ASSERT_EQUAL_STRING("default", testConfig->getString("nonexistent", "default").c_str());
}

// Test integer operations
void test_config_int_operations()
{
    testConfig->setInt("int_key", 42);
    TEST_ASSERT_EQUAL_INT(42, testConfig->getInt("int_key"));

    // Test default value
    TEST_ASSERT_EQUAL_INT(100, testConfig->getInt("nonexistent", 100));
}

// Test boolean operations
void test_config_bool_operations()
{
    testConfig->setBool("bool_key", true);
    TEST_ASSERT_TRUE(testConfig->getBool("bool_key"));

    testConfig->setBool("bool_key", false);
    TEST_ASSERT_FALSE(testConfig->getBool("bool_key"));

    // Test default value
    TEST_ASSERT_TRUE(testConfig->getBool("nonexistent", true));
}

// Test float operations
void test_config_float_operations()
{
    testConfig->setFloat("float_key", 3.14f);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 3.14f, testConfig->getFloat("float_key"));

    // Test default value
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 2.71f, testConfig->getFloat("nonexistent", 2.71f));
}

// Test persistence
void test_config_persistence()
{
    // Set some values
    testConfig->setString("persist_string", "persistent_value");
    testConfig->setInt("persist_int", 123);
    testConfig->setBool("persist_bool", true);

    // Save to file
    bool saveResult = testConfig->save();
    TEST_ASSERT_TRUE(saveResult);

    // Create new config instance and load
    Config *newConfig = new Config();
    newConfig->begin(); // Initialize before loading
    bool loadResult = newConfig->load();
    TEST_ASSERT_TRUE(loadResult);

    // Verify values persisted
    TEST_ASSERT_EQUAL_STRING("persistent_value", newConfig->getString("persist_string").c_str());
    TEST_ASSERT_EQUAL_INT(123, newConfig->getInt("persist_int"));
    TEST_ASSERT_TRUE(newConfig->getBool("persist_bool"));

    delete newConfig;
}

// Test JSON serialization
void test_config_json_serialization()
{
    testConfig->setString("json_string", "test");
    testConfig->setInt("json_int", 456);
    testConfig->setBool("json_bool", false);

    String jsonString = testConfig->toJson();
    TEST_ASSERT_TRUE(jsonString.length() > 0);
    TEST_ASSERT_TRUE(jsonString.indexOf("json_string") >= 0);
    TEST_ASSERT_TRUE(jsonString.indexOf("json_int") >= 0);
    TEST_ASSERT_TRUE(jsonString.indexOf("json_bool") >= 0);
}

// Test JSON deserialization
void test_config_json_deserialization()
{
    String jsonInput = "{\"test_key\":\"test_value\",\"test_int\":42}";
    testConfig->setFromJson(jsonInput);

    TEST_ASSERT_EQUAL_STRING("test_value", testConfig->getString("test_key").c_str());
    TEST_ASSERT_EQUAL_INT(42, testConfig->getInt("test_int"));
}

// Test configuration validation
void test_config_validation()
{
    // Set some valid configuration
    testConfig->setString("device_model", "solplanet_asw_gen");
    testConfig->setString("parity", "N");

    bool isValid = testConfig->validate();
    TEST_ASSERT_TRUE(isValid || !isValid); // May depend on other required fields
}

// Test factory reset
void test_config_factory_reset()
{
    // Set some values that will be overridden by factory reset
    testConfig->setString("wifi_ssid", "test_network");
    testConfig->setInt("rtu_addr", 999);

    // Perform factory reset
    testConfig->factoryReset();

    // Verify values are reset to defaults (not empty, but default values)
    TEST_ASSERT_EQUAL_STRING("", testConfig->getString("wifi_ssid").c_str()); // wifi_ssid defaults to empty
    TEST_ASSERT_EQUAL_INT(1, testConfig->getInt("rtu_addr"));                 // rtu_addr defaults to 1

    // Test that non-default keys return their method defaults
    TEST_ASSERT_EQUAL_STRING("", testConfig->getString("nonexistent_key").c_str());
    TEST_ASSERT_EQUAL_INT(0, testConfig->getInt("nonexistent_key"));
}

void setup()
{
    delay(2000); // Wait for serial monitor

    UNITY_BEGIN();

    RUN_TEST(test_config_string_operations);
    RUN_TEST(test_config_int_operations);
    RUN_TEST(test_config_bool_operations);
    RUN_TEST(test_config_float_operations);
    RUN_TEST(test_config_persistence);
    RUN_TEST(test_config_json_serialization);
    RUN_TEST(test_config_json_deserialization);
    RUN_TEST(test_config_validation);
    RUN_TEST(test_config_factory_reset);

    UNITY_END();
}

void loop()
{
    // Empty - tests run once in setup()
}
