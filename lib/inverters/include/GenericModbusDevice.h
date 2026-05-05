#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <vector>
#include "InverterInterface.h"
#include "Config.h"
#include "ModbusClient.h"

/**
 * @brief Generic Modbus device that reads configuration from JSON
 *
 * Allows adding new devices without recompiling firmware.
 * JSON format defines registers, scaling, and units.
 */
class GenericModbusDevice : public InverterInterface
{
public:
    explicit GenericModbusDevice(ModbusClient &modbus, Config &config, const String &jsonFile);
    bool begin() override;
    bool readBasic(std::vector<TelemetryPoint> &out) override;
    bool readStorage(std::vector<TelemetryPoint> &out) override;

private:
    ModbusClient &modbus;
    Config &config;
    String jsonFilePath;
    uint8_t slaveAddr;
    bool initialized;

    // Register definition from JSON
    struct RegisterDef
    {
        uint16_t addr;
        String name;
        uint8_t fc;           // Function code (3 or 4)
        String type;          // "uint16", "uint32", "int16", "int32"
        float scale;
        String unit;
        bool isStorage;       // true if this is a storage/battery register
    };

    std::vector<RegisterDef> registers;
    String deviceName;
    String deviceId;

    // JSON parsing
    bool loadJSON();
    bool parseRegisterDef(JsonObject regObj, RegisterDef &def);

    // Register reading
    bool readRegister(const RegisterDef &def, float &value);
    int getStorageRegisterCount() const;
};
