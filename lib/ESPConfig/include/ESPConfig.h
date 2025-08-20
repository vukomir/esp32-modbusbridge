#ifndef ESP_CONFIG_H
#define ESP_CONFIG_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include "constants.h"

struct WiFiConfig
{
    String ssid;
    String password;
    bool apEnabled;
    String apSsid;
    String apPassword;
};

struct MQTTConfig
{
    String broker;
    uint16_t port;
    String username;
    String password;
    String topicPrefix;
    bool retain;
    uint16_t keepalive;
};

struct ModbusConfig
{
    uint8_t rtuAddr;
    uint32_t baudrate;
    char parity;
    uint8_t stopBits;
    uint8_t rs485DeRePin;
    uint16_t timeoutMs;
    uint8_t maxRetries;
};

struct InverterConfig
{
    String model;
    bool readStorageRegs;
    uint16_t pollIntervalSec;
};

struct SystemConfig
{
    String deviceName;
    String firmwareVersion;
    bool debugMode;
    uint32_t watchdogTimeoutMs;
};

struct DeviceConfig
{
    WiFiConfig wifi;
    MQTTConfig mqtt;
    ModbusConfig modbus;
    InverterConfig inverter;
    SystemConfig system;
};

class ESPConfig
{
public:
    static void begin();
    static bool load();
    static bool save();
    static bool backup();
    static bool restore();
    static void reset();

    // Configuration access
    static DeviceConfig &getConfig();
    static const DeviceConfig &getConfig() const;

    // Individual section updates
    static bool updateWiFi(const WiFiConfig &config);
    static bool updateMQTT(const MQTTConfig &config);
    static bool updateModbus(const ModbusConfig &config);
    static bool updateInverter(const InverterConfig &config);
    static bool updateSystem(const SystemConfig &config);

    // Validation
    static bool validateConfig(const DeviceConfig &config);
    static bool validateWiFi(const WiFiConfig &config);
    static bool validateMQTT(const MQTTConfig &config);
    static bool validateModbus(const ModbusConfig &config);

    // Utilities
    static String getDeviceId();
    static String configToJson();
    static bool configFromJson(const String &json);

    // Callbacks for config changes
    typedef std::function<void(const DeviceConfig &)> ConfigChangeCallback;
    static void setConfigChangeCallback(ConfigChangeCallback callback);

private:
    static DeviceConfig config;
    static ConfigChangeCallback changeCallback;

    static void loadDefaults();
    static bool loadFromFile(const char *path);
    static bool saveToFile(const char *path);
    static void notifyChange();
};

#endif // ESP_CONFIG_H
