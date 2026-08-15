#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <LittleFS.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <ESPLogger.h>

#include "Config.h"
#include "LogStore.h"
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

    // First thing after the UART, and deliberately before ESPLogger::begin():
    // everything logged from here on - LittleFS mount, config load,
    // wifiManager.begin() - lands in RTC RAM and survives into the next boot.
    // WebUI registers its own sink much later (main.cpp calls webUI.begin()
    // below), which is precisely why an early boot hang used to leave no record.
    LogStore::begin();
    ESPLogger::addLogCallback(LogStore::sink);

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
    if (!wifiManager.begin())
    {
        ESPLogger::warn("WiFi did not come up during setup() - handleConnection() will keep retrying");
    }
    webUI.begin(80);

    mqttClient.begin();
    modbusClient.begin();

    poller.begin();

    // The pin map is static text that never varies between boots, so it is one
    // line rather than five. Everything logged during setup() competes for the
    // protected boot segment in RTC memory (see lib/log_store).
    ESPLogger::info("Wiring: MAX485 DE/RE=GPIO4, RO=GPIO16, DI=GPIO17, factory reset=GPIO21");

    ESPLogger::info("=== System Ready ===");
    if (wifiManager.isConnected())
    {
        ESPLogger::info("Access web UI at: http://%s/", wifiManager.getIPAddress().c_str());
    }
    else
    {
        ESPLogger::info("Connect to AP: %s and go to http://192.168.4.1/", wifiManager.getDeviceId().c_str());
    }

    // Create optimized FreeRTOS tasks for ESP32 dual-core architecture.
    // One summary line after the fact rather than a line per task: the stack
    // and core assignments are fixed at compile time, and statusTask reports
    // live high-water marks every 30s anyway.

    // Core 1: WebSocket/HTTP with larger stack for WebSocket operations
    xTaskCreatePinnedToCore(webTask, "WebTask", 6144, NULL, 1, &webTaskHandle, 1);

    // Core 0: MQTT (network I/O, medium priority)
    xTaskCreatePinnedToCore(mqttTask, "MQTTTask", 4096, NULL, 2, &mqttTaskHandle, 0);

    // Core 0: Modbus polling (I/O intensive, higher priority)
    // 6KB stack: 4KB was the original budget for SolplanetASW's 23-register block reads.
    // GenericModbusDevice can issue up to 125-register block reads with a 250-byte
    // stack buffer plus deeper call chain (decodeValue, std::vector pushes, String
    // allocations), so we lift the ceiling to 6KB. RAM cost: +2KB permanently.
    xTaskCreatePinnedToCore(pollerTask, "PollerTask", 6144, NULL, 3, &pollerTaskHandle, 0);

    // Core 1: Status reporting (low priority, background)
    xTaskCreatePinnedToCore(statusTask, "StatusTask", 4096, NULL, 1, &statusTaskHandle, 1);

    // Core 1: Button monitoring (low priority, small stack)
    xTaskCreatePinnedToCore(buttonTask, "ButtonTask", 2048, NULL, 1, &buttonTaskHandle, 1);

    // 6 (web) + 4 (mqtt) + 6 (poller) + 4 (status) + 2 (button) = 22 KB
    ESPLogger::info("Tasks started: web/6K+status/4K+button/2K on core 1, "
                    "mqtt/4K+poller/6K on core 0. Final heap: %u bytes",
                    ESP.getFreeHeap());
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
        // Wait first, report second. The old order fired this ~3s into boot,
        // putting seven records of routine periodic output straight into the
        // protected boot segment for no benefit - heap and stack high-water
        // marks that early are not yet meaningful anyway.
        vTaskDelay(pdMS_TO_TICKS(30000));

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
