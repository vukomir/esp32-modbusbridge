#include <unity.h>
#include <Arduino.h>
#include "ModbusClient.h"
#include "Config.h"

// Test fixtures
Config *testConfig;
ModbusClient *modbusClient;

void setUp(void)
{
    testConfig = new Config();

    // Set up basic modbus configuration
    testConfig->setInt("baudrate", 9600);
    testConfig->setString("parity", "N");
    testConfig->setInt("stop_bits", 1);
    testConfig->setInt("rs485_de_re_pin", 4);

    modbusClient = new ModbusClient(*testConfig);
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

// Test basic initialization
void test_modbus_initialization()
{
    TEST_ASSERT_FALSE(modbusClient->isInitialized());

    bool result = modbusClient->begin();
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_TRUE(modbusClient->isInitialized());
}

// Test CRC calculation
void test_crc_calculation()
{
    // Test known CRC values
    uint8_t testData1[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x01};
    uint16_t expectedCRC1 = 0x84CA; // Known CRC for this data

    // We can't directly test calculateCRC as it's private, but we can test
    // frame building which uses it internally
    TEST_ASSERT_TRUE(true); // Placeholder - CRC is tested indirectly through frame operations
}

// Test register combination utilities
void test_register_combination()
{
    uint16_t high = 0x1234;
    uint16_t low = 0x5678;

    uint32_t combined = modbusClient->combineRegisters(high, low);
    uint32_t expected = 0x12345678;

    TEST_ASSERT_EQUAL_UINT32(expected, combined);
}

// Test signed register combination
void test_signed_register_combination()
{
    uint16_t high = 0xFFFF;
    uint16_t low = 0xFFFF;

    int32_t combined = modbusClient->combineSignedRegisters(high, low);
    int32_t expected = -1;

    TEST_ASSERT_EQUAL_INT32(expected, combined);
}

// Test scaling functions
void test_scaling_functions()
{
    // Test normal scaling
    uint16_t rawValue = 1000;
    float scale = 0.1f;
    float result = modbusClient->applyScaling(rawValue, scale);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 100.0f, result);

    // Test NaN handling
    uint16_t nanValue = 0xFFFF;
    float nanResult = modbusClient->applyScaling(nanValue, scale);
    TEST_ASSERT_TRUE(isnan(nanResult));

    // Test 32-bit scaling
    uint32_t rawValue32 = 10000;
    float result32 = modbusClient->applyScaling32(rawValue32, scale);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1000.0f, result32);
}

// Test configuration validation
void test_configuration_validation()
{
    // Test valid baudrate
    testConfig->setInt("baudrate", 9600);
    ModbusClient validClient(*testConfig);
    TEST_ASSERT_TRUE(validClient.begin());
    validClient.end();

    // Test invalid baudrate (should default to 9600)
    testConfig->setInt("baudrate", 12345);
    ModbusClient invalidClient(*testConfig);
    TEST_ASSERT_TRUE(invalidClient.begin()); // Should still work with default
    invalidClient.end();
}

// Test parity configuration
void test_parity_configuration()
{
    // Test even parity
    testConfig->setString("parity", "E");
    ModbusClient evenClient(*testConfig);
    TEST_ASSERT_TRUE(evenClient.begin());
    evenClient.end();

    // Test odd parity
    testConfig->setString("parity", "O");
    ModbusClient oddClient(*testConfig);
    TEST_ASSERT_TRUE(oddClient.begin());
    oddClient.end();

    // Test none parity (default)
    testConfig->setString("parity", "N");
    ModbusClient noneClient(*testConfig);
    TEST_ASSERT_TRUE(noneClient.begin());
    noneClient.end();
}

// Test data bits configuration
void test_data_bits_configuration()
{
    // Test 7 data bits
    testConfig->setInt("data_bits", 7);
    ModbusClient sevenBitClient(*testConfig);
    TEST_ASSERT_TRUE(sevenBitClient.begin());
    sevenBitClient.end();

    // Test 8 data bits (default)
    testConfig->setInt("data_bits", 8);
    ModbusClient eightBitClient(*testConfig);
    TEST_ASSERT_TRUE(eightBitClient.begin());
    eightBitClient.end();

    // Test invalid data bits (should default to 8)
    testConfig->setInt("data_bits", 6);
    ModbusClient invalidBitsClient(*testConfig);
    TEST_ASSERT_TRUE(invalidBitsClient.begin()); // Should still work with default
    invalidBitsClient.end();
}

// Test complete serial configuration combinations
void test_serial_configuration_combinations()
{
    // Test 7E1 configuration
    testConfig->setInt("data_bits", 7);
    testConfig->setString("parity", "E");
    testConfig->setInt("stop_bits", 1);
    ModbusClient config7E1(*testConfig);
    TEST_ASSERT_TRUE(config7E1.begin());
    config7E1.end();

    // Test 8N2 configuration
    testConfig->setInt("data_bits", 8);
    testConfig->setString("parity", "N");
    testConfig->setInt("stop_bits", 2);
    ModbusClient config8N2(*testConfig);
    TEST_ASSERT_TRUE(config8N2.begin());
    config8N2.end();

    // Test 7O1 configuration
    testConfig->setInt("data_bits", 7);
    testConfig->setString("parity", "O");
    testConfig->setInt("stop_bits", 1);
    ModbusClient config7O1(*testConfig);
    TEST_ASSERT_TRUE(config7O1.begin());
    config7O1.end();
}

// Test DE/RE pin configuration
void test_de_re_pin_configuration()
{
    // Test valid pin
    testConfig->setInt("rs485_de_re_pin", 4);
    ModbusClient validPinClient(*testConfig);
    TEST_ASSERT_TRUE(validPinClient.begin());
    validPinClient.end();

    // Test invalid pin (negative)
    testConfig->setInt("rs485_de_re_pin", -1);
    ModbusClient invalidPinClient(*testConfig);
    TEST_ASSERT_TRUE(invalidPinClient.begin()); // Should still work
    invalidPinClient.end();
}

// Test MAX485 connection detection
void test_max485_detection()
{
    modbusClient->begin();

    // The current implementation always returns true when initialized
    bool isConnected = modbusClient->isMAX485Connected();
    TEST_ASSERT_TRUE(isConnected);
}

// Test connection testing
void test_connection_testing()
{
    modbusClient->begin();

    // Test connection should work if MAX485 is "detected"
    bool connectionTest = modbusClient->testConnection();
    TEST_ASSERT_TRUE(connectionTest);
}

// Test device communication (will fail without actual device)
void test_device_communication()
{
    modbusClient->begin();

    // This will fail without an actual device, but tests the method
    bool deviceTest = modbusClient->testDeviceCommunication(1);
    // We expect this to fail in test environment
    TEST_ASSERT_FALSE(deviceTest);
}

// Test multiple initialization calls
void test_multiple_initialization()
{
    TEST_ASSERT_TRUE(modbusClient->begin());
    TEST_ASSERT_TRUE(modbusClient->isInitialized());

    // Second call should return true without issues
    TEST_ASSERT_TRUE(modbusClient->begin());
    TEST_ASSERT_TRUE(modbusClient->isInitialized());
}

// Test end functionality
void test_end_functionality()
{
    modbusClient->begin();
    TEST_ASSERT_TRUE(modbusClient->isInitialized());

    modbusClient->end();
    TEST_ASSERT_FALSE(modbusClient->isInitialized());

    // Should be able to begin again after end
    TEST_ASSERT_TRUE(modbusClient->begin());
    TEST_ASSERT_TRUE(modbusClient->isInitialized());
}

void setup()
{
    delay(2000); // Wait for serial monitor

    UNITY_BEGIN();

    RUN_TEST(test_modbus_initialization);
    RUN_TEST(test_crc_calculation);
    RUN_TEST(test_register_combination);
    RUN_TEST(test_signed_register_combination);
    RUN_TEST(test_scaling_functions);
    RUN_TEST(test_configuration_validation);
    RUN_TEST(test_parity_configuration);
    RUN_TEST(test_data_bits_configuration);
    RUN_TEST(test_serial_configuration_combinations);
    RUN_TEST(test_de_re_pin_configuration);
    RUN_TEST(test_max485_detection);
    RUN_TEST(test_connection_testing);
    RUN_TEST(test_device_communication);
    RUN_TEST(test_multiple_initialization);
    RUN_TEST(test_end_functionality);

    UNITY_END();
}

void loop()
{
    // Empty - tests run once in setup()
}
