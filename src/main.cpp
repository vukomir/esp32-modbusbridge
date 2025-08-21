#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <LittleFS.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <ESPLogger.h>

#include "Config.h"
#include "WiFiManager.h"
#include "WebUI.h"
#include "MQTTClient.h"
#include "ModbusClient.h"
#include "ModbusScanner.h"
#include "Poller.h"

// Global instances
static Config config;
static WiFiManager wifiManager(config);
static WebUI webUI(config, wifiManager);
static MQTTClient mqttClient(config, wifiManager);
static ModbusClient modbusClient(config);
static Poller poller(config, mqttClient, modbusClient);

// Modbus device scanning on boot
void runModbusBootScan();

// FreeRTOS task handles
TaskHandle_t webTaskHandle = NULL;
TaskHandle_t mqttTaskHandle = NULL;
TaskHandle_t pollerTaskHandle = NULL;
TaskHandle_t statusTaskHandle = NULL;

// FreeRTOS task functions
void webTask(void *parameter);
void mqttTask(void *parameter);
void pollerTask(void *parameter);
void statusTask(void *parameter);

void runModbusBootScan()
{
    ESPLogger::info("🔍 === MODBUS DEVICE SCAN ON BOOT ===");
    
    if (!modbusClient.isInitialized())
    {
        ESPLogger::error("❌ Modbus client not initialized - skipping scan");
        return;
    }
    
    // Check MAX485 status first
    bool max485Connected = modbusClient.isMAX485Connected();
    ESPLogger::info("📡 MAX485 Status: %s", max485Connected ? "✅ CONNECTED" : "❌ NOT CONNECTED");
    
    if (!max485Connected)
    {
        ESPLogger::error("❌ MAX485 not detected - cannot scan for devices");
        ESPLogger::error("🔧 Check MAX485 module wiring to GPIO4");
        return;
    }
    
    // Display current configuration
    ESPLogger::info("📋 Current Modbus Configuration:");
    ESPLogger::info("   Device Model: %s", config.getString("device_model", "unknown").c_str());
    ESPLogger::info("   Configured Slave Address: %d", config.getInt("rtu_addr", 1));
    ESPLogger::info("   Serial Config: %d baud, %d%c%d", 
                   config.getInt("baudrate", 9600),
                   config.getInt("data_bits", 8),
                   config.getString("parity", "N").charAt(0),
                   config.getInt("stop_bits", 1));
    
    // Create scanner and scan for devices
    ModbusScanner scanner(modbusClient);
    
    ESPLogger::info("🔍 Scanning for Modbus devices (addresses 1-20)...");
    auto foundDevices = scanner.scanAddresses(1, 20);
    
    // Count responding devices
    int respondingCount = 0;
    for (const auto& device : foundDevices)
    {
        if (device.responding)
        {
            respondingCount++;
        }
    }
    
    ESPLogger::info("📊 Scan Results: Found %d responding device(s)", respondingCount);
    
    if (respondingCount > 0)
    {
        ESPLogger::info("✅ Discovered Modbus Devices:");
        for (const auto& device : foundDevices)
        {
            if (device.responding)
            {
                ESPLogger::info("   📍 Slave %d: Value=%d, Response=%lums, Type=%s", 
                               device.slaveId, 
                               device.testRegisterValue,
                               device.responseTime,
                               device.serialConfig.c_str());
            }
        }
        
        // Check if configured device is responding
        uint8_t configuredAddr = config.getInt("rtu_addr", 1);
        bool configuredDeviceFound = false;
        
        for (const auto& device : foundDevices)
        {
            if (device.slaveId == configuredAddr && device.responding)
            {
                configuredDeviceFound = true;
                break;
            }
        }
        
        if (configuredDeviceFound)
        {
            ESPLogger::info("✅ Configured device (address %d) is responding", configuredAddr);
        }
        else
        {
            ESPLogger::warn("⚠️  Configured device (address %d) NOT responding", configuredAddr);
            ESPLogger::warn("💡 Consider updating RTU address to match a responding device");
        }
        
        // Publish scan results to MQTT
        if (mqttClient.isInitialized())
        {
            String scanResults = "{\"responding_devices\":" + String(respondingCount) + 
                               ",\"configured_address\":" + String(configuredAddr) +
                               ",\"configured_responding\":" + (configuredDeviceFound ? "true" : "false") + "}";
            mqttClient.publishDiagnostics("modbus_scan", scanResults);
        }
    }
    else
    {
        ESPLogger::warn("❌ No Modbus devices found");
        ESPLogger::warn("🔧 Troubleshooting checklist:");
        ESPLogger::warn("   1. Check RS485 A/B wiring");
        ESPLogger::warn("   2. Verify device power");
        ESPLogger::warn("   3. Confirm device slave address");
        ESPLogger::warn("   4. Try different baud rates");
        ESPLogger::warn("   5. Check RS485 termination resistors");
        
        // Publish no devices found status
        if (mqttClient.isInitialized())
        {
            String noDevicesResult = "{\"responding_devices\":0,\"scan_completed\":true,\"timestamp\":" + 
                                   String(millis()) + "}";
            mqttClient.publishDiagnostics("modbus_scan", noDevicesResult);
        }
    }
    
    ESPLogger::info("🔍 === BOOT SCAN COMPLETE ===");
}

void setup()
{
    Serial.begin(115200);
    delay(1000);

    ESPLogger::begin(); // Initialize ESPLogger first
    ESPLogger::info("Starting Modbus Bridge...");
    ESPLogger::info("Free heap at start: %u bytes", ESP.getFreeHeap());

    // Filesystem and Config
    if (!LittleFS.begin())
    {
        LittleFS.format();
        LittleFS.begin();
        ESPLogger::warn("LittleFS formatted and mounted");
    }
    else
    {
        ESPLogger::info("LittleFS mounted successfully");
    }
    config.begin("/config.json");

    // Apply log level from configuration
    String logLevel = config.getString("log_level", "info");
    ESPLogger::setLevel(ESPLogger::stringToLogLevel(logLevel.c_str()));

    ESPLogger::info("Configuration loaded");
    ESPLogger::info("Log level set to: %s", logLevel.c_str());
    ESPLogger::info("Free heap after config: %u bytes", ESP.getFreeHeap());

    // Initialize components
    wifiManager.begin();
    webUI.begin(80);

    // ESP32 has plenty of memory - enable all components
    ESPLogger::info("ESP32 detected - enabling all components");

    mqttClient.begin();
    modbusClient.begin();
    
    // Run Modbus device scan on boot
    runModbusBootScan();
    
    poller.begin();

    ESPLogger::info("");
    ESPLogger::info("All components enabled!");
    ESPLogger::info("- ESP32 D2 (GPIO4) → MAX485 DE/RE");
    ESPLogger::info("- ESP32 D16 (GPIO16) → MAX485 RO");
    ESPLogger::info("- ESP32 D17 (GPIO17) → MAX485 DI");
    ESPLogger::info("- Connect MAX485 A/B to device RS485 terminals");

    ESPLogger::info("=== System Ready ===");
    if (wifiManager.isConnected())
    {
        ESPLogger::info("Access web UI at: http://%s/", wifiManager.getIPAddress().c_str());
    }
    else
    {
        ESPLogger::info("Connect to AP: %s and go to http://192.168.4.1/", wifiManager.getDeviceId().c_str());
    }

    // Create optimized FreeRTOS tasks for ESP32 dual-core architecture
    ESPLogger::info("Creating FreeRTOS tasks...");
    ESPLogger::info("Free heap before task creation: %u bytes", ESP.getFreeHeap());

    // Create all FreeRTOS tasks with optimized stack sizes
    ESPLogger::info("Creating all FreeRTOS tasks with optimized stacks...");

    // Core 1: WebSocket/HTTP with larger stack for WebSocket operations
    xTaskCreatePinnedToCore(webTask, "WebTask", 6144, NULL, 1, &webTaskHandle, 1);
    ESPLogger::info("WebTask created on Core 1 (6KB stack)");

    // Core 0: MQTT (network I/O, medium priority)
    xTaskCreatePinnedToCore(mqttTask, "MQTTTask", 4096, NULL, 2, &mqttTaskHandle, 0);
    ESPLogger::info("MQTTTask created on Core 0 (4KB stack)");

    // Core 0: Modbus polling (I/O intensive, higher priority)
    xTaskCreatePinnedToCore(pollerTask, "PollerTask", 4096, NULL, 3, &pollerTaskHandle, 0);
    ESPLogger::info("PollerTask created on Core 0 (4KB stack)");

    // Core 1: Status reporting (low priority, background)
    xTaskCreatePinnedToCore(statusTask, "StatusTask", 4096, NULL, 1, &statusTaskHandle, 1);
    ESPLogger::info("StatusTask created on Core 1 (4KB stack)");

    ESPLogger::info("All tasks created successfully!");
    ESPLogger::info("Total stack allocated: 18KB, Final heap: %u bytes", ESP.getFreeHeap());
}

void loop()
{
    // Main loop only handles critical WiFi management
    // All other services run in dedicated FreeRTOS tasks
    wifiManager.handleConnection();
    delay(10);
}

// FreeRTOS Task Implementations

void webTask(void *parameter)
{
    ESPLogger::info("WebTask started successfully");
    for (;;)
    {
        try
        {
            webUI.handleClient();
        }
        catch (...)
        {
            ESPLogger::error("WebTask exception caught!");
        }
        vTaskDelay(pdMS_TO_TICKS(20)); // 20ms delay
    }
}

void mqttTask(void *parameter)
{
    ESPLogger::info("MQTTTask started successfully");
    for (;;)
    {
        try
        {
            if (mqttClient.isInitialized())
            {
                mqttClient.handleConnection();
            }
        }
        catch (...)
        {
            ESPLogger::error("MQTTTask exception caught!");
        }
        vTaskDelay(pdMS_TO_TICKS(200)); // 200ms delay
    }
}

void pollerTask(void *parameter)
{
    for (;;)
    {
        if (poller.isInitialized())
        {
            poller.poll();
        }
        vTaskDelay(pdMS_TO_TICKS(5000)); // 5 second delay
    }
}

void statusTask(void *parameter)
{
    for (;;)
    {
        ESPLogger::info("Status - Free heap: %d bytes", ESP.getFreeHeap());
        if (wifiManager.isConnected())
        {
            ESPLogger::info("WiFi: %s (%s)", wifiManager.getSSID().c_str(), wifiManager.getIPAddress().c_str());
        }
        if (mqttClient.isConnected())
        {
            ESPLogger::info("MQTT: Connected");
        }
        vTaskDelay(pdMS_TO_TICKS(30000)); // 30 second delay
    }
}
