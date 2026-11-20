#pragma once

#include <Arduino.h>
#include <PubSubClient.h>
#ifdef ESP32
#include <WiFi.h>
#else
#include <ESP8266WiFi.h>
#endif
#include "Config.h"
#include "WiFiManager.h"
#include "constants.h"
#include "InverterFactory.h"

/**
 * @brief MQTT client wrapper with automatic reconnection and topic management
 * Provides stable topic generation and reliable publishing with backoff
 */
class MQTTClient
{
public:
    MQTTClient(Config &config, WiFiManager &wifiManager);
    ~MQTTClient();

    bool begin();
    void handleConnection();
    bool isConnected();
    bool isInitialized() const;
    void disconnect();

    // Publishing (Legacy format - backward compatibility)
    bool publish(const String &metric, const String &value, bool retain = true);
    bool publishStatus(const String &status);
    bool publishRSSI();
    bool publishUptime();

    // Publishing (New generic format)
    bool publishTelemetry(const String &metric, const String &value, const String &unit = "", bool retain = true);
    bool publishStatus(const String &metric, const String &value, bool retain = true);
    bool publishDiagnostics(const String &metric, const String &value, bool retain = true);
    bool publishHardware(const String &metric, const String &value, bool retain = true);
    bool publishConfig(const String &metric, const String &value, bool retain = true);

    // Publish device-under-monitoring availability ("online" / "offline").
    // Retained so Home Assistant picks up the last known state on subscribe.
    // NOTE: this is the *Modbus device* availability, not the ESP32's own MQTT
    //       presence — that should be handled separately via MQTT LWT.
    bool publishAvailability(bool online, bool retain = true);

    // JSON publishing
    bool publishJson(const String &dataType, const String &metric, const String &jsonPayload, bool retain = true);

    // Connection management
    bool connect();
    void reconnect();

private:
    Config &config;
    WiFiManager &wifiManager;
    WiFiClient wifiClient;
    PubSubClient mqttClient;

    bool initialized;
    unsigned long lastConnectAttempt;
    unsigned long connectInterval;
    int connectAttempts;
    unsigned long lastStatusPublish;

    static const unsigned long MIN_CONNECT_INTERVAL = 5000;     // 5 seconds
    static const unsigned long MAX_CONNECT_INTERVAL = 300000;   // 5 minutes
    static const unsigned long STATUS_PUBLISH_INTERVAL = 60000; // 1 minute
    static const int MAX_CONNECT_ATTEMPTS = 10;

    // Topic management (Legacy)
    String buildTopic(const String &metric) const;

    // Topic management (New generic format)
    String buildGenericTopic(const String &dataType, const String &metric) const;
    String getTopicPrefix() const;
    String getDeviceType() const;
    String getDeviceId() const;

    // Connection helpers
    void updateConnectInterval();
    bool validateMQTTConfig() const;
    void publishConnectStatus();
};
