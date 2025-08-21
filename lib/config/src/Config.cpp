#include "Config.h"
#include <ESPLogger.h>
#include "constants.h"

Config::Config() : isLoaded(false)
{
}

Config::~Config()
{
}

bool Config::begin(const String &configPath)
{
    this->configPath = configPath;
    if (!LittleFS.begin())
    {
        ESPLogger::error("Failed to mount LittleFS");
        return false;
    }
    return load();
}

void Config::setDefaults()
{
    config["wifi_ssid"] = "";
    config["wifi_password"] = "";
    config["ap_enabled"] = true;
    config["ap_ssid"] = "modbusbridge-setup";
    config["ap_password"] = "setup1234";
    config["hostname"] = "modbusbridge";
    config["log_level"] = "info";

    config["mqtt_broker"] = "192.168.1.10";
    config["mqtt_port"] = 1883;
    config["mqtt_username"] = "";
    config["mqtt_password"] = "";
    config["mqtt_topic_prefix"] = "home";
    config["device_type"] = "inverter";
    config["device_id"] = "auto";
    config["mqtt_retain"] = true;
    config["mqtt_json_format"] = true;

    // Legacy field for backward compatibility
    config["retain"] = true;

    config["rtu_addr"] = 1;
    config["baudrate"] = 9600;
    config["parity"] = "N";
    config["data_bits"] = 8;
    config["stop_bits"] = 1;
    config["rs485_de_re_pin"] = 4;

    config["poll_interval_sec"] = 10;
    config["device_model"] = DEFAULT_DEVICE_MODEL;
    config["read_storage_regs"] = false;
}

bool Config::load()
{
    if (!LittleFS.exists(configPath))
    {
        ESPLogger::info("Config file not found, using defaults");
        setDefaults();
        isLoaded = true;
        return save(); // Create with defaults
    }

    File file = LittleFS.open(configPath, "r");
    if (!file)
    {
        ESPLogger::error("Failed to open config file for reading");
        setDefaults();
        isLoaded = true;
        return false;
    }

    DeserializationError error = deserializeJson(config, file);
    file.close();

    if (error)
    {
        ESPLogger::error("Failed to parse config JSON: %s", error.c_str());
        setDefaults();
        isLoaded = true;
        return false;
    }

    // Apply any missing defaults
    JsonDocument defaults;
    Config temp;
    temp.setDefaults();
    copyArray(temp.config, defaults);

    for (JsonPair kv : defaults.as<JsonObject>())
    {
        if (config[kv.key().c_str()].isNull())
        {
            config[kv.key().c_str()] = kv.value();
        }
    }

    isLoaded = true;
    return validate();
}

bool Config::save()
{
    if (!validate())
    {
        ESPLogger::error("Cannot save invalid configuration");
        return false;
    }

    File file = LittleFS.open(configPath, "w");
    if (!file)
    {
        ESPLogger::error("Failed to open config file for writing");
        return false;
    }

    size_t written = serializeJsonPretty(config, file);
    file.close();

    if (written == 0)
    {
        ESPLogger::error("Failed to write config to file");
        return false;
    }

    ESPLogger::info("Configuration saved successfully");
    return true;
}

bool Config::factoryReset()
{
    if (LittleFS.exists(configPath))
    {
        LittleFS.remove(configPath);
    }
    setDefaults();
    isLoaded = true;
    return save();
}

bool Config::validate()
{
    // Validate device model
    if (!isValidDeviceModel(getString("device_model")))
    {
        ESPLogger::error("Invalid device model");
        return false;
    }

    // Validate parity
    if (!isValidParity(getString("parity")))
    {
        ESPLogger::error("Invalid parity setting");
        return false;
    }

    // Validate data bits
    int dataBits = getInt("data_bits", 8);
    if (dataBits != 7 && dataBits != 8)
    {
        ESPLogger::error("Invalid data bits, must be 7 or 8");
        return false;
    }

    // Validate stop bits
    int stopBits = getInt("stop_bits", 1);
    if (stopBits != 1 && stopBits != 2)
    {
        ESPLogger::error("Invalid stop bits, must be 1 or 2");
        return false;
    }

    // Validate numeric ranges
    int baudrate = getInt("baudrate");
    if (baudrate != 9600 && baudrate != 19200 && baudrate != 38400)
    {
        ESPLogger::error("Invalid baudrate, must be 9600, 19200, or 38400");
        return false;
    }

    int rtuAddr = getInt("rtu_addr");
    if (rtuAddr < 1 || rtuAddr > 247)
    {
        ESPLogger::error("Invalid RTU address, must be 1-247");
        return false;
    }

    int pollInterval = getInt("poll_interval_sec");
    if (pollInterval < 1 || pollInterval > 3600)
    {
        ESPLogger::error("Invalid poll interval, must be 1-3600 seconds");
        return false;
    }

    return true;
}

bool Config::isValidDeviceModel(const String &model) const
{
    for (size_t i = 0; i < SUPPORTED_DEVICES_COUNT; i++)
    {
        if (model == SUPPORTED_DEVICES[i].model)
        {
            return true;
        }
    }
    return false;
}

bool Config::isValidParity(const String &parity) const
{
    return parity == "N" || parity == "E" || parity == "O";
}

String Config::getString(const String &key, const String &defaultValue) const
{
    if (!isLoaded || config[key].isNull())
    {
        return defaultValue;
    }
    return config[key].as<String>();
}

int Config::getInt(const String &key, int defaultValue) const
{
    if (!isLoaded || config[key].isNull())
    {
        return defaultValue;
    }
    return config[key].as<int>();
}

bool Config::getBool(const String &key, bool defaultValue) const
{
    if (!isLoaded || config[key].isNull())
    {
        return defaultValue;
    }
    return config[key].as<bool>();
}

float Config::getFloat(const String &key, float defaultValue) const
{
    if (!isLoaded || config[key].isNull())
    {
        return defaultValue;
    }
    return config[key].as<float>();
}

void Config::setString(const String &key, const String &value)
{
    config[key] = value;
}

void Config::setInt(const String &key, int value)
{
    config[key] = value;
}

void Config::setBool(const String &key, bool value)
{
    config[key] = value;
}

void Config::setFloat(const String &key, float value)
{
    config[key] = value;
}

void Config::setFromJson(const String &jsonString)
{
    JsonDocument newConfig;
    DeserializationError error = deserializeJson(newConfig, jsonString);
    if (!error)
    {
        config = newConfig;
    }
}

String Config::toJson() const
{
    String result;
    serializeJsonPretty(config, result);
    return result;
}

void Config::applyDefaults()
{
    setDefaults();
}
