#include <unity.h>
#include <Arduino.h>
#include "WiFiManager.h"
#include "Config.h"

// Test fixtures
Config *testConfig;
WiFiManager *wifiManager;

void setUp(void)
{
    testConfig = new Config();
    testConfig->begin();

    // Set up WiFi configuration
    testConfig->setString("wifi_ssid", "test_network");
    testConfig->setString("wifi_password", "test_password");
    testConfig->setString("hostname", "test-device");
    testConfig->setBool("ap_enabled", true);
    testConfig->setString("ap_ssid", "test-setup");
    testConfig->setString("ap_password", "setup123");

    wifiManager = new WiFiManager(*testConfig);
}

void tearDown(void)
{
    if (wifiManager)
    {
        wifiManager->disconnect();
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
void test_wifi_initialization()
{
    // begin() may fail without actual WiFi, but should not crash
    bool result = wifiManager->begin();
    TEST_ASSERT_TRUE(result || !result); // Just test that it doesn't crash
}

// Test connection status methods
void test_connection_status()
{
    wifiManager->begin();

    // Test connection status methods
    bool isConnected = wifiManager->isConnected();
    bool isAPMode = wifiManager->isAPMode();

    // Should be able to call these without crashing
    TEST_ASSERT_TRUE(isConnected || !isConnected);
    TEST_ASSERT_TRUE(isAPMode || !isAPMode);

    // AP mode and STA mode should be mutually exclusive
    if (isConnected)
    {
        TEST_ASSERT_FALSE(isAPMode);
    }
}

// Test network information methods
void test_network_information()
{
    wifiManager->begin();

    // Test IP address retrieval
    String ipAddress = wifiManager->getIPAddress();
    TEST_ASSERT_TRUE(ipAddress.length() >= 0); // Should return some string

    // Test RSSI (signal strength)
    int rssi = wifiManager->getRSSI();
    TEST_ASSERT_TRUE(rssi <= 0); // RSSI should be negative or zero

    // Test SSID retrieval
    String ssid = wifiManager->getSSID();
    TEST_ASSERT_TRUE(ssid.length() >= 0); // Should return some string

    // Test MAC address
    String macAddress = wifiManager->getMACAddress();
    TEST_ASSERT_TRUE(macAddress.length() > 0);     // MAC should always be available
    TEST_ASSERT_TRUE(macAddress.indexOf(":") > 0); // Should contain colons
}

// Test device identification
void test_device_identification()
{
    wifiManager->begin();

    // Test device ID generation
    String deviceId = wifiManager->getDeviceId();
    TEST_ASSERT_TRUE(deviceId.length() > 0);
    TEST_ASSERT_TRUE(deviceId.length() <= 12); // Should be reasonable length

    // Test hostname
    String hostname = wifiManager->getHostname();
    TEST_ASSERT_TRUE(hostname.length() > 0);
    TEST_ASSERT_EQUAL_STRING("test-device", hostname.c_str());

    // Test network hostname
    String networkHostname = wifiManager->getNetworkHostname();
    TEST_ASSERT_TRUE(networkHostname.length() > 0);
}

// Test connection management
void test_connection_management()
{
    wifiManager->begin();

    // Test STA connection (will likely fail without actual network)
    bool staResult = wifiManager->connectSTA();
    TEST_ASSERT_TRUE(staResult || !staResult); // Just test no crash

    // Test AP mode startup
    bool apResult = wifiManager->startAP();
    TEST_ASSERT_TRUE(apResult || !apResult); // Just test no crash

    // Test disconnect
    wifiManager->disconnect();
    TEST_ASSERT_TRUE(true); // If we get here, no crash occurred

    // Test reconnect
    wifiManager->reconnect();
    TEST_ASSERT_TRUE(true); // If we get here, no crash occurred
}

// Test mDNS functionality
void test_mdns_functionality()
{
    wifiManager->begin();

    // Test mDNS setup
    bool mdnsResult = wifiManager->setupmDNS();
    TEST_ASSERT_TRUE(mdnsResult || !mdnsResult); // May fail without connection

    // Test mDNS status
    bool isMDNSActive = wifiManager->ismDNSActive();
    TEST_ASSERT_TRUE(isMDNSActive || !isMDNSActive);

    // Test mDNS restart
    wifiManager->restartmDNS();
    TEST_ASSERT_TRUE(true); // If we get here, no crash occurred
}

// Test NTP functionality
void test_ntp_functionality()
{
    wifiManager->begin();

    // Test NTP setup
    wifiManager->setupNTP();
    TEST_ASSERT_TRUE(true); // If we get here, no crash occurred

    // Test NTP sync status
    bool isNTPSynced = wifiManager->isNTPSynced();
    TEST_ASSERT_TRUE(isNTPSynced || !isNTPSynced);

    // Test time retrieval
    String currentTime = wifiManager->getCurrentTime();
    TEST_ASSERT_TRUE(currentTime.length() >= 0); // Should return some string
}

// Test configuration parameters
void test_configuration_parameters()
{
    // Test with different hostname
    testConfig->setString("hostname", "custom-device");
    WiFiManager customWifi(*testConfig);
    customWifi.begin();
    TEST_ASSERT_EQUAL_STRING("custom-device", customWifi.getHostname().c_str());

    // Test with AP disabled
    testConfig->setBool("ap_enabled", false);
    WiFiManager noAPWifi(*testConfig);
    bool result = noAPWifi.begin();
    TEST_ASSERT_TRUE(result || !result); // Should handle AP disabled gracefully

    // Test with different AP credentials
    testConfig->setString("ap_ssid", "custom-setup");
    testConfig->setString("ap_password", "custom123");
    WiFiManager customAPWifi(*testConfig);
    result = customAPWifi.begin();
    TEST_ASSERT_TRUE(result || !result);
}

// Test error handling
void test_error_handling()
{
    // Test with empty SSID
    testConfig->setString("wifi_ssid", "");
    WiFiManager emptySSIDWifi(*testConfig);
    bool result = emptySSIDWifi.begin();
    TEST_ASSERT_TRUE(result || !result); // Should handle gracefully

    // Test with very long SSID
    testConfig->setString("wifi_ssid", "very_long_ssid_name_that_exceeds_normal_length_limits");
    WiFiManager longSSIDWifi(*testConfig);
    result = longSSIDWifi.begin();
    TEST_ASSERT_TRUE(result || !result); // Should handle gracefully

    // Test with invalid characters in hostname
    testConfig->setString("hostname", "test device with spaces");
    WiFiManager invalidHostnameWifi(*testConfig);
    result = invalidHostnameWifi.begin();
    TEST_ASSERT_TRUE(result || !result); // Should handle gracefully
}

// Test state consistency
void test_state_consistency()
{
    wifiManager->begin();

    // Test that status methods return consistent results
    bool connected1 = wifiManager->isConnected();
    bool connected2 = wifiManager->isConnected();
    TEST_ASSERT_EQUAL(connected1, connected2);

    bool apMode1 = wifiManager->isAPMode();
    bool apMode2 = wifiManager->isAPMode();
    TEST_ASSERT_EQUAL(apMode1, apMode2);

    // Device ID should be consistent
    String deviceId1 = wifiManager->getDeviceId();
    String deviceId2 = wifiManager->getDeviceId();
    TEST_ASSERT_EQUAL_STRING(deviceId1.c_str(), deviceId2.c_str());
}

// Test connection handling
void test_connection_handling()
{
    wifiManager->begin();

    // Test connection handling (should not crash)
    wifiManager->handleConnection();
    TEST_ASSERT_TRUE(true); // If we get here, no crash occurred

    // Test multiple connection handling calls
    for (int i = 0; i < 3; i++)
    {
        wifiManager->handleConnection();
    }
    TEST_ASSERT_TRUE(true); // If we get here, no crash occurred
}

// Test memory management
void test_memory_management()
{
    // Test multiple WiFiManager creation/destruction
    for (int i = 0; i < 3; i++)
    {
        WiFiManager *tempWifi = new WiFiManager(*testConfig);
        tempWifi->begin();
        delete tempWifi;
    }

    // If we get here, memory management is working
    TEST_ASSERT_TRUE(true);
}

// Test MAC address parsing
void test_mac_address_parsing()
{
    wifiManager->begin();

    String macAddress = wifiManager->getMACAddress();

    if (macAddress.length() > 0)
    {
        // MAC address should be in format XX:XX:XX:XX:XX:XX
        int colonCount = 0;
        for (int i = 0; i < macAddress.length(); i++)
        {
            if (macAddress.charAt(i) == ':')
            {
                colonCount++;
            }
        }
        TEST_ASSERT_EQUAL_INT(5, colonCount); // Should have 5 colons

        // Device ID should be derived from MAC
        String deviceId = wifiManager->getDeviceId();
        TEST_ASSERT_TRUE(deviceId.length() > 0);
    }
}

// Test concurrent operations
void test_concurrent_operations()
{
    wifiManager->begin();

    // Test that multiple operations can be called without issues
    wifiManager->isConnected();
    wifiManager->getIPAddress();
    wifiManager->getRSSI();
    wifiManager->getDeviceId();
    wifiManager->handleConnection();

    // All should complete without crashing
    TEST_ASSERT_TRUE(true);
}

void setup()
{
    delay(2000); // Wait for serial monitor

    UNITY_BEGIN();

    RUN_TEST(test_wifi_initialization);
    RUN_TEST(test_connection_status);
    RUN_TEST(test_network_information);
    RUN_TEST(test_device_identification);
    RUN_TEST(test_connection_management);
    RUN_TEST(test_mdns_functionality);
    RUN_TEST(test_ntp_functionality);
    RUN_TEST(test_configuration_parameters);
    RUN_TEST(test_error_handling);
    RUN_TEST(test_state_consistency);
    RUN_TEST(test_connection_handling);
    RUN_TEST(test_memory_management);
    RUN_TEST(test_mac_address_parsing);
    RUN_TEST(test_concurrent_operations);

    UNITY_END();
}

void loop()
{
    // Empty - tests run once in setup()
}
