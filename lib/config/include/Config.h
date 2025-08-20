#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h>

/**
 * @brief Configuration manager for inverter monitoring device
 * Handles loading, saving, and validation of JSON configuration from LittleFS
 */
class Config
{
public:
    Config();
    ~Config();

    bool begin(const String &configPath = "/config.json");
    bool load();
    bool save();
    bool factoryReset();
    bool validate();

    // Getters
    String getString(const String &key, const String &defaultValue = "") const;
    int getInt(const String &key, int defaultValue = 0) const;
    bool getBool(const String &key, bool defaultValue = false) const;
    float getFloat(const String &key, float defaultValue = 0.0f) const;

    // Setters
    void setString(const String &key, const String &value);
    void setInt(const String &key, int value);
    void setBool(const String &key, bool value);
    void setFloat(const String &key, float value);

    // Bulk operations
    void setFromJson(const String &jsonString);
    String toJson() const;
    void applyDefaults();

private:
    String configPath;
    JsonDocument config;
    bool isLoaded;

    void setDefaults();
    bool isValidDeviceModel(const String &model) const;
    bool isValidParity(const String &parity) const;
};
