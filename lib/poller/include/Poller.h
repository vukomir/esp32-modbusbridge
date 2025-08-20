#pragma once

#include <Arduino.h>
#include <vector>
#include <memory>
#include "Config.h"
#include "InverterInterface.h"
#include "constants.h"

// Forward declarations
class MQTTClient;
class ModbusClient;

/**
 * @brief Telemetry poller that reads inverter data and publishes to MQTT
 * Orchestrates the reading of inverter registers and publishing of metrics
 */
class Poller
{
public:
    Poller(Config &config, MQTTClient &mqtt, ModbusClient &modbus);
    ~Poller();

    bool begin();
    void poll();
    bool isInitialized() const;

    // Statistics
    unsigned long getLastPollTime() const;
    unsigned long getSuccessfulPolls() const;
    unsigned long getFailedPolls() const;

private:
    Config &config;
    MQTTClient &mqtt;
    ModbusClient &modbus;
    std::unique_ptr<InverterInterface> inverter;

    bool initialized;
    unsigned long lastPollTime;
    unsigned long successfulPolls;
    unsigned long failedPolls;
    String lastError;

    // Polling logic
    bool initializeInverter();
    bool readAndPublishTelemetry();
    void publishTelemetryPoints(const std::vector<TelemetryPoint> &points);
    void publishPollStatus(bool success, const String &error = "");
    void publishHardwareStatus(const String &component, const String &status, const String &message);
};
