#include "Poller.h"
#include "MQTTClient.h"
#include "ModbusClient.h"
#include "InverterFactory.h"
#include <ESPLogger.h>
#include "constants.h"
#include <ArduinoJson.h>

Poller::Poller(Config &config, MQTTClient &mqtt, ModbusClient &modbus)
    : config(config), mqtt(mqtt), modbus(modbus), initialized(false),
      lastPollTime(0), successfulPolls(0), failedPolls(0),
      consecutiveFailures(0), publishedAvailability(AvailabilityState::Unknown)
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

    ESPLogger::info("Initializing Poller...");
    ESPLogger::info("Free heap before Poller init: %u bytes", ESP.getFreeHeap());

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
    ESPLogger::info("Free heap after Poller init: %u bytes", ESP.getFreeHeap());
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
        updateAvailability(false);
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
        updateAvailability(false);
        return;
    }

    // NOTE: the previous pre-poll testDeviceCommunication() call was removed
    // because it queries DDS238 meter registers (0x000C-0x000E), which causes
    // SolPlanet inverters to reply with Modbus exception 0x02 ("Illegal Data
    // Address"). That exception was being treated as failure, blocking the
    // real telemetry read for any non-DDS238 device. The actual telemetry
    // read below is the source of truth — if the device is unreachable, it
    // will fail with the right error and updateAvailability() handles it.

    // Determine device model/type for hardware status reporting after the read.
    String deviceModel = config.getString("device_model", DEFAULT_DEVICE_MODEL);
    String deviceType = InverterFactory::getDeviceType(deviceModel);

    bool success = readAndPublishTelemetry();
    if (success)
    {
        successfulPolls++;
        publishPollStatus(true);
        publishHardwareStatus(deviceType, "connected", deviceModel + " responding");
        ESPLogger::info("✅ %s CONNECTED and responding via RS485", deviceModel.c_str());
        ESPLogger::debug("Poll successful - Basic: %lu total", successfulPolls);
    }
    else
    {
        failedPolls++;
        publishPollStatus(false, lastError);
        publishHardwareStatus(deviceType, "not_responding", "No response from configured device");
        ESPLogger::error("Poll failed - %s", lastError.c_str());
    }
    updateAvailability(success);
}

void Poller::updateAvailability(bool pollSucceeded)
{
    if (pollSucceeded)
    {
        if (consecutiveFailures > 0)
        {
            ESPLogger::info("Poll recovered after %lu consecutive failure(s)", consecutiveFailures);
        }
        consecutiveFailures = 0;
        if (publishedAvailability != AvailabilityState::Online)
        {
            if (mqtt.publishAvailability(true))
            {
                publishedAvailability = AvailabilityState::Online;
                ESPLogger::info("Availability → online");
            }
            else
            {
                ESPLogger::warn("Failed to publish availability=online; will retry next poll");
                // leave publishedAvailability as-is so we retry on next success
            }
        }
        return;
    }

    consecutiveFailures++;
    if (consecutiveFailures >= MAX_CONSECUTIVE_FAILURES &&
        publishedAvailability != AvailabilityState::Offline)
    {
        if (mqtt.publishAvailability(false))
        {
            publishedAvailability = AvailabilityState::Offline;
            ESPLogger::warn("Availability → offline (after %lu consecutive failures)", consecutiveFailures);
        }
        else
        {
            ESPLogger::warn("Failed to publish availability=offline; will retry next poll");
        }
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
    // Use ArduinoJson to avoid heap fragmentation from String concatenation
    StaticJsonDocument<256> doc;
    doc["success"] = success;
    doc["timestamp"] = lastPollTime;
    doc["uptime"] = millis() / 1000;
    doc["successful_polls"] = successfulPolls;
    doc["failed_polls"] = failedPolls;

    if (!success && !error.isEmpty())
    {
        doc["error"] = error;
    }

    String jsonStatus;
    serializeJson(doc, jsonStatus);

    // Use new generic topic structure for status
    mqtt.publishJson(MQTT_DATA_TYPE_STATUS, MQTT_METRIC_POLL_STATUS, jsonStatus, true);
}

void Poller::publishHardwareStatus(const String &component, const String &status, const String &message)
{
    // Use ArduinoJson to avoid heap fragmentation
    StaticJsonDocument<256> doc;
    doc["component"] = component;
    doc["status"] = status;
    doc["message"] = message;
    doc["timestamp"] = millis();
    doc["uptime"] = millis() / 1000;

    String jsonStatus;
    serializeJson(doc, jsonStatus);

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
