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
#include "Poller.h"

// Global instances
static Config config;
static WiFiManager wifiManager(config);
static WebUI webUI(config, wifiManager);
static MQTTClient mqttClient(config, wifiManager);
static ModbusClient modbusClient(config);
static Poller poller(config, mqttClient, modbusClient);

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

    // WiFi Manager
    wifiManager.begin();
    ESPLogger::info("WiFi manager started");
    ESPLogger::info("Free heap after WiFi: %u bytes", ESP.getFreeHeap());

    // Web UI
    webUI.begin(80);
    ESPLogger::info("Web UI started on port 80");
    ESPLogger::info("Free heap after WebUI: %u bytes", ESP.getFreeHeap());

    // ESP32 has plenty of memory - enable all components
    ESPLogger::info("ESP32 detected - enabling all components");

    // Re-enable all components now that device is stable
    ESPLogger::info("Re-enabling all components with optimized memory usage...");

    // Initialize MQTT
    ESPLogger::info("Initializing MQTT client...");
    ESPLogger::info("Free heap before MQTT init: %u bytes", ESP.getFreeHeap());
    mqttClient.begin();
    ESPLogger::info("MQTT initialized successfully");
    ESPLogger::info("Free heap after MQTT init: %u bytes", ESP.getFreeHeap());

    // Initialize Modbus (now using Serial2)
    ESPLogger::info("Initializing Modbus client...");
    ESPLogger::info("Free heap before Modbus init: %u bytes", ESP.getFreeHeap());
    modbusClient.begin();
    ESPLogger::info("Modbus initialized successfully");
    ESPLogger::info("Free heap after Modbus init: %u bytes", ESP.getFreeHeap());

    // Initialize Poller
    ESPLogger::info("Initializing Poller...");
    ESPLogger::info("Free heap before Poller init: %u bytes", ESP.getFreeHeap());
    poller.begin();
    ESPLogger::info("Poller initialized successfully");
    ESPLogger::info("Free heap after Poller init: %u bytes", ESP.getFreeHeap());

    ESPLogger::info("");
    ESPLogger::info("All components enabled!");
    ESPLogger::info("- ESP32 D1 (GPIO5) → MAX485 DE/RE");
    ESPLogger::info("- ESP32 D2 (GPIO4) → MAX485 RO");
    ESPLogger::info("- ESP32 D3 (GPIO0) → MAX485 DI");
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
