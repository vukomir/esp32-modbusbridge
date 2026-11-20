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

    // Availability tracking. After MAX_CONSECUTIVE_FAILURES failed polls in a row
    // we publish availability=offline so Home Assistant stops trusting retained
    // telemetry. We re-publish availability=online on the first success after.
    // Initial value is "unknown until first poll" — see updateAvailability().
    unsigned long consecutiveFailures;
    enum class AvailabilityState { Unknown, Online, Offline };
    AvailabilityState publishedAvailability;
    static constexpr unsigned long MAX_CONSECUTIVE_FAILURES = 3;

    // Status topics are published when they change, plus a periodic refresh
    // for the poll counters. Publishing all three on every cycle was fine at a
    // 10s interval; at 1s it is three retained messages a second that say
    // nothing new.
    static constexpr unsigned long STATUS_REFRESH_MS = 30000;
    enum class PollStatusState { Unknown, Ok, Failed };
    PollStatusState publishedPollStatus;
    unsigned long lastPollStatusPublishMs;

    // One per hardware status topic: the RS485 transceiver and the device
    // behind it.
    struct HardwareStatusSlot
    {
        String published;             // last status that reached the broker
        String logged;                // last status written to the log
        unsigned long publishedMs = 0;
    };
    HardwareStatusSlot max485StatusSlot;
    HardwareStatusSlot deviceStatusSlot;

    // Polling logic
    bool initializeInverter();
    bool readAndPublishTelemetry();
    void publishTelemetryPoints(const std::vector<TelemetryPoint> &points);
    void publishPollStatus(bool success, const String &error = "");
    void publishHardwareStatus(const String &component, const String &status, const String &message);
    // Bumps the failure counter and publishes availability transitions.
    // Call exactly once per poll(), AFTER all per-path status publishes.
    void updateAvailability(bool pollSucceeded);
};
