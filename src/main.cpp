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
static ModbusClient modbusClient(config);
static WebUI webUI(config, wifiManager, modbusClient);
static MQTTClient mqttClient(config, wifiManager);
static Poller poller(config, mqttClient, modbusClient);

// Factory reset button configuration
const int FACTORY_RESET_BUTTON_PIN = 21;             // GPIO21 (dedicated factory reset button)
const unsigned long FACTORY_RESET_HOLD_TIME = 10000; // 10 seconds
unsigned long buttonPressStartTime = 0;
bool buttonPressed = false;
bool factoryResetInProgress = false;

// FreeRTOS task handles
TaskHandle_t webTaskHandle = NULL;
TaskHandle_t mqttTaskHandle = NULL;
TaskHandle_t pollerTaskHandle = NULL;
TaskHandle_t statusTaskHandle = NULL;
TaskHandle_t buttonTaskHandle = NULL;

// Mutex for Config access (protects against concurrent read/write during config save)
SemaphoreHandle_t configMutex = NULL;

// FreeRTOS task functions
void webTask(void *parameter);
void mqttTask(void *parameter);
void pollerTask(void *parameter);
void statusTask(void *parameter);
void buttonTask(void *parameter);

void setup()
{
    Serial.begin(115200);
    delay(1000);

    ESPLogger::begin(); // Initialize ESPLogger first
    ESPLogger::info("Starting Modbus Bridge...");
    ESPLogger::info("Free heap at start: %u bytes", ESP.getFreeHeap());

    // Configure factory reset button
    pinMode(FACTORY_RESET_BUTTON_PIN, INPUT_PULLUP);
    ESPLogger::info("Factory reset button configured on GPIO%d (hold 10s for reset)", FACTORY_RESET_BUTTON_PIN);

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

    // Create mutex for Config access (protects against race conditions during config save)
    configMutex = xSemaphoreCreateMutex();
    if (configMutex == NULL)
    {
        ESPLogger::error("Failed to create config mutex!");
    }
    else
    {
        ESPLogger::info("Config mutex created successfully");
    }

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

    poller.begin();

    ESPLogger::info("");
    ESPLogger::info("All components enabled!");
    ESPLogger::info("- ESP32 D2 (GPIO4) → MAX485 DE/RE");
    ESPLogger::info("- ESP32 D16 (GPIO16) → MAX485 RO");
    ESPLogger::info("- ESP32 D17 (GPIO17) → MAX485 DI");
    ESPLogger::info("- ESP32 D21 (GPIO21) → Factory Reset Button");
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
    // 6KB stack: 4KB was the original budget for SolplanetASW's 23-register block reads.
    // GenericModbusDevice can issue up to 125-register block reads with a 250-byte
    // stack buffer plus deeper call chain (decodeValue, std::vector pushes, String
    // allocations), so we lift the ceiling to 6KB. RAM cost: +2KB permanently.
    xTaskCreatePinnedToCore(pollerTask, "PollerTask", 6144, NULL, 3, &pollerTaskHandle, 0);
    ESPLogger::info("PollerTask created on Core 0 (6KB stack)");

    // Core 1: Status reporting (low priority, background)
    xTaskCreatePinnedToCore(statusTask, "StatusTask", 4096, NULL, 1, &statusTaskHandle, 1);
    ESPLogger::info("StatusTask created on Core 1 (4KB stack)");

    // Core 1: Button monitoring (low priority, small stack)
    xTaskCreatePinnedToCore(buttonTask, "ButtonTask", 2048, NULL, 1, &buttonTaskHandle, 1);
    ESPLogger::info("ButtonTask created on Core 1 (2KB stack)");

    ESPLogger::info("All tasks created successfully!");
    // 6 (web) + 4 (mqtt) + 6 (poller) + 4 (status) + 2 (button) = 22 KB
    ESPLogger::info("Total stack allocated: 22KB, Final heap: %u bytes", ESP.getFreeHeap());
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
    // Cache poll interval - read once at task start to avoid repeated config lookups
    int pollIntervalSec = config.getInt("poll_interval_sec", 10);
    ESPLogger::info("Poller task started with %d second interval", pollIntervalSec);

    for (;;)
    {
        if (poller.isInitialized())
        {
            poller.poll();
        }

        vTaskDelay(pdMS_TO_TICKS(pollIntervalSec * 1000));
    }
}

void statusTask(void *parameter)
{
    for (;;)
    {
        // Memory status
        ESPLogger::info("Status - Free heap: %d bytes", ESP.getFreeHeap());

        // Stack high-water marks (lower = more stack used, <100 words = warning threshold)
        if (webTaskHandle != NULL)
        {
            UBaseType_t webStack = uxTaskGetStackHighWaterMark(webTaskHandle);
            ESPLogger::info("WebTask stack free: %u words%s", webStack, webStack < 100 ? " ⚠️ LOW" : "");
        }
        if (mqttTaskHandle != NULL)
        {
            UBaseType_t mqttStack = uxTaskGetStackHighWaterMark(mqttTaskHandle);
            ESPLogger::info("MQTTTask stack free: %u words%s", mqttStack, mqttStack < 100 ? " ⚠️ LOW" : "");
        }
        if (pollerTaskHandle != NULL)
        {
            UBaseType_t pollerStack = uxTaskGetStackHighWaterMark(pollerTaskHandle);
            ESPLogger::info("PollerTask stack free: %u words%s", pollerStack, pollerStack < 100 ? " ⚠️ LOW" : "");
        }
        if (buttonTaskHandle != NULL)
        {
            UBaseType_t buttonStack = uxTaskGetStackHighWaterMark(buttonTaskHandle);
            ESPLogger::info("ButtonTask stack free: %u words%s", buttonStack, buttonStack < 100 ? " ⚠️ LOW" : "");
        }

        // Network status
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

void buttonTask(void *parameter)
{
    unsigned long buttonPressStart = 0;
    bool buttonWasPressed = false;

    for (;;)
    {
        bool buttonCurrentlyPressed = (digitalRead(FACTORY_RESET_BUTTON_PIN) == LOW);

        if (buttonCurrentlyPressed && !buttonWasPressed)
        {
            // Button just pressed
            buttonPressStart = millis();
            buttonWasPressed = true;
            ESPLogger::info("🔘 Factory reset button pressed - hold for 10 seconds");
        }
        else if (!buttonCurrentlyPressed && buttonWasPressed)
        {
            // Button released
            unsigned long holdTime = millis() - buttonPressStart;
            buttonWasPressed = false;

            if (holdTime >= 10000)
            {
                ESPLogger::warn("🏭 Factory reset triggered! (held for %lu ms)", holdTime);

                // Flash LED to show reset starting (using vTaskDelay instead of delay)
                digitalWrite(2, HIGH);
                vTaskDelay(pdMS_TO_TICKS(500));
                digitalWrite(2, LOW);
                vTaskDelay(pdMS_TO_TICKS(500));
                digitalWrite(2, HIGH);
                vTaskDelay(pdMS_TO_TICKS(500));
                digitalWrite(2, LOW);

                // Perform factory reset - exact same code as WebUI
                ESPLogger::warn("🏭 Performing factory reset...");
                if (config.factoryReset())
                {
                    ESPLogger::warn("✅ Factory reset completed! Device will restart in setup mode...");

                    // Success pattern - slow blinks
                    for (int i = 0; i < 5; i++)
                    {
                        digitalWrite(2, HIGH);
                        vTaskDelay(pdMS_TO_TICKS(300));
                        digitalWrite(2, LOW);
                        vTaskDelay(pdMS_TO_TICKS(300));
                    }

                    vTaskDelay(pdMS_TO_TICKS(3000));
                    ESP.restart();
                }
                else
                {
                    ESPLogger::error("❌ Factory reset failed");

                    // Error pattern - fast blinks
                    for (int i = 0; i < 10; i++)
                    {
                        digitalWrite(2, HIGH);
                        vTaskDelay(pdMS_TO_TICKS(50));
                        digitalWrite(2, LOW);
                        vTaskDelay(pdMS_TO_TICKS(50));
                    }
                }
            }
            else
            {
                ESPLogger::info("🔘 Button released after %lu ms (need 10000ms)", holdTime);
            }
        }
        else if (buttonCurrentlyPressed && buttonWasPressed)
        {
            // Button being held - show progress
            unsigned long holdTime = millis() - buttonPressStart;

            // Show progress every 2 seconds
            if (holdTime % 2000 < 200) // Show message in first 200ms of each 2-second period
            {
                unsigned long remaining = (10000 - holdTime) / 1000;
                if (remaining > 0)
                {
                    ESPLogger::info("🔘 Hold for %lu more seconds for factory reset...", remaining);
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(200)); // Check every 200ms
    }
}
