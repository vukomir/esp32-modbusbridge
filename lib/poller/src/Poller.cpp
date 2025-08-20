#include "Poller.h"
#include "MQTTClient.h"
#include "ModbusClient.h"
#include "InverterFactory.h"
#include <ESPLogger.h>
#include "constants.h"

Poller::Poller(Config &config, MQTTClient &mqtt, ModbusClient &modbus)
    : config(config), mqtt(mqtt), modbus(modbus), initialized(false),
      lastPollTime(0), successfulPolls(0), failedPolls(0)
{
}

Poller::~Poller()
{
}

bool Poller::begin()
{
    if (initialized)
    {
        return true;
    }

    if (!modbus.isInitialized())
    {
        ESPLogger::error("Modbus client not initialized");
        return false;
    }

    if (!initializeInverter())
    {
        ESPLogger::error("Failed to initialize inverter");
        return false;
    }

    initialized = true;
    ESPLogger::info("Poller initialized successfully");
    return true;
}

void Poller::poll()
{
    if (!initialized)
    {
        ESPLogger::error("Poller not initialized");
        return;
    }

    lastPollTime = millis();

    // Check hardware status first
    ESPLogger::info("=== Hardware Status Check (Poll #%lu) ===", successfulPolls + failedPolls + 1);

    // Check if MAX485 is connected
    ESPLogger::debug("Checking MAX485 connection...");
    bool max485Connected = modbus.isMAX485Connected();
    ESPLogger::info("MAX485 detection result: %s", max485Connected ? "CONNECTED" : "NOT CONNECTED");

    if (!max485Connected)
    {
        // MAX485 not detected
        ESPLogger::error("❌ MAX485 module NOT DETECTED on DE/RE pin - check wiring to GPIO4");
        publishHardwareStatus("MAX485", "not_connected", "MAX485 module not detected on DE/RE pin");
        publishPollStatus(false, "MAX485 module not connected");
        failedPolls++;
        ESPLogger::warn("Poll skipped - MAX485 module not connected");
        return;
    }

    // MAX485 is connected, test communication
    ESPLogger::info("✅ MAX485 module DETECTED and responding on GPIO4");
    publishHardwareStatus("MAX485", "connected", "MAX485 module detected");

    // Basic hardware test first
    if (!modbus.testConnection())
    {
        ESPLogger::warn("⚠️  MAX485 hardware check failed");
        publishHardwareStatus("MAX485", "not_connected", "MAX485 module not detected on DE/RE pin");
        publishPollStatus(false, "MAX485 module not connected");
        failedPolls++;
        return;
    }

    // Test actual device communication with configured slave address
    uint8_t slaveAddr = config.getInt("rtu_addr", 1);
    if (!modbus.testDeviceCommunication(slaveAddr))
    {
        // MAX485 connected but no device response
        ESPLogger::warn("⚠️  Device NOT RESPONDING on slave address %d - check RS485 A/B wiring", slaveAddr);
        publishHardwareStatus("device", "not_responding", "No response from configured device");
        publishPollStatus(false, "Device not responding");
        failedPolls++;
        ESPLogger::warn("Poll skipped - Device not responding on address %d", slaveAddr);
        return;
    }

    // Both MAX485 and inverter are responding
    ESPLogger::info("✅ SolPlanet inverter CONNECTED and responding via RS485");
    publishHardwareStatus("inverter", "connected", "SolPlanet inverter responding");

    bool success = readAndPublishTelemetry();
    if (success)
    {
        successfulPolls++;
        publishPollStatus(true);
        ESPLogger::debug("Poll successful - Basic: %lu total", successfulPolls);
    }
    else
    {
        failedPolls++;
        publishPollStatus(false, lastError);
        ESPLogger::error("Poll failed - %s", lastError.c_str());
    }
}

bool Poller::isInitialized() const
{
    return initialized;
}

unsigned long Poller::getLastPollTime() const
{
    return lastPollTime;
}

unsigned long Poller::getSuccessfulPolls() const
{
    return successfulPolls;
}

unsigned long Poller::getFailedPolls() const
{
    return failedPolls;
}

bool Poller::initializeInverter()
{
    String model = config.getString("device_model", DEFAULT_DEVICE_MODEL);

    inverter = InverterFactory::create(model, modbus, config);
    if (!inverter)
    {
        lastError = "Unknown inverter model: " + model;
        return false;
    }

    if (!inverter->begin())
    {
        lastError = "Inverter initialization failed";
        return false;
    }

    ESPLogger::info("Inverter initialized: %s", model.c_str());
    return true;
}

bool Poller::readAndPublishTelemetry()
{
    if (!inverter)
    {
        lastError = "No inverter instance";
        return false;
    }

    // Read basic telemetry
    std::vector<TelemetryPoint> basicPoints;
    bool basicSuccess = inverter->readBasic(basicPoints);

    if (!basicSuccess)
    {
        lastError = "Failed to read basic telemetry";
        return false;
    }

    // Publish basic telemetry
    publishTelemetryPoints(basicPoints);
    ESPLogger::debug("Published %zu basic telemetry points", basicPoints.size());

    // Read storage telemetry if enabled
    if (config.getBool("read_storage_regs", false))
    {
        std::vector<TelemetryPoint> storagePoints;
        bool storageSuccess = inverter->readStorage(storagePoints);

        if (storageSuccess && !storagePoints.empty())
        {
            publishTelemetryPoints(storagePoints);
            ESPLogger::debug("Published %zu storage telemetry points", storagePoints.size());
        }
        else if (!storageSuccess)
        {
            ESPLogger::error("Storage telemetry read failed - continuing with basic telemetry");
        }
    }

    return true;
}

void Poller::publishTelemetryPoints(const std::vector<TelemetryPoint> &points)
{
    for (const auto &point : points)
    {
        if (!point.value.isEmpty())
        {
            // Use new generic topic structure for telemetry
            bool success = mqtt.publishTelemetry(point.name, point.value, point.unit, true);
            if (!success)
            {
                ESPLogger::error("Failed to publish telemetry: %s", point.name.c_str());
            }
        }
    }
}

void Poller::publishPollStatus(bool success, const String &error)
{
    // Create JSON object for poll status
    String jsonStatus = "{"
                        "\"success\":" +
                        String(success ? "true" : "false") + ","
                                                             "\"timestamp\":" +
                        String(lastPollTime) + ","
                                               "\"uptime\":" +
                        String(millis() / 1000) + ","
                                                  "\"successful_polls\":" +
                        String(successfulPolls) + ","
                                                  "\"failed_polls\":" +
                        String(failedPolls);

    if (!success && !error.isEmpty())
    {
        jsonStatus += ",\"error\":\"" + error + "\"";
    }

    jsonStatus += "}";

    // Use new generic topic structure for status
    mqtt.publishJson(MQTT_DATA_TYPE_STATUS, MQTT_METRIC_POLL_STATUS, jsonStatus, true);
}

void Poller::publishHardwareStatus(const String &component, const String &status, const String &message)
{
    // Create JSON object for hardware status
    String jsonStatus = "{"
                        "\"component\":\"" +
                        component + "\","
                                    "\"status\":\"" +
                        status + "\","
                                 "\"message\":\"" +
                        message + "\","
                                  "\"timestamp\":" +
                        String(millis()) + ","
                                           "\"uptime\":" +
                        String(millis() / 1000) +
                        "}";

    // Use new generic topic structure for hardware status
    String metric;
    if (component == "MAX485")
    {
        metric = MQTT_METRIC_MAX485_STATUS;
    }
    else if (component == "inverter")
    {
        metric = MQTT_METRIC_INVERTER_STATUS;
    }
    else
    {
        // Fallback for unknown components
        metric = component;
        metric.toLowerCase();
        metric += "_status";
    }
    mqtt.publishJson(MQTT_DATA_TYPE_HARDWARE, metric, jsonStatus, true);

    ESPLogger::info("Hardware status: %s = %s (%s)", component.c_str(), status.c_str(), message.c_str());
}
