#include "GenericModbusDevice.h"
#include <ESPLogger.h>
#include <LittleFS.h>

GenericModbusDevice::GenericModbusDevice(ModbusClient &modbus, Config &config, const String &jsonFile)
    : modbus(modbus), config(config), jsonFilePath(jsonFile), slaveAddr(1), initialized(false)
{
    slaveAddr = config.getInt("rtu_addr", 1);
}

bool GenericModbusDevice::begin()
{
    if (initialized)
    {
        return true;
    }

    ESPLogger::info("Initializing Generic Modbus Device from JSON: %s", jsonFilePath.c_str());

    if (!loadJSON())
    {
        ESPLogger::error("Failed to load JSON configuration");
        return false;
    }

    ESPLogger::info("Loaded device: %s (%s) with %d registers",
                    deviceName.c_str(), deviceId.c_str(), registers.size());

    initialized = true;
    return true;
}

bool GenericModbusDevice::loadJSON()
{
    // LittleFS should already be mounted by main.cpp - don't remount
    if (!LittleFS.exists(jsonFilePath))
    {
        ESPLogger::error("JSON device config not found: %s", jsonFilePath.c_str());
        ESPLogger::error("Upload the JSON file via /diagnostics -> Device JSON Manager");
        return false;
    }

    File file = LittleFS.open(jsonFilePath, "r");
    if (!file)
    {
        ESPLogger::error("Failed to open JSON file: %s", jsonFilePath.c_str());
        return false;
    }

    // Parse JSON
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, file);
    file.close();

    if (error)
    {
        ESPLogger::error("JSON parse error: %s", error.c_str());
        return false;
    }

    // Extract device info
    deviceId = doc["device_id"] | "unknown";
    deviceName = doc["name"] | "Unknown Device";

    // Parse registers array
    JsonArray regsArray = doc["registers"];
    if (!regsArray)
    {
        ESPLogger::error("No 'registers' array in JSON");
        return false;
    }

    for (JsonObject regObj : regsArray)
    {
        RegisterDef def;
        if (parseRegisterDef(regObj, def))
        {
            registers.push_back(def);
        }
    }

    if (registers.empty())
    {
        ESPLogger::error("No valid registers found in JSON");
        return false;
    }

    return true;
}

bool GenericModbusDevice::parseRegisterDef(JsonObject regObj, RegisterDef &def)
{
    // Parse address (hex or decimal)
    String addrStr = regObj["addr"] | "0x0000";
    def.addr = (uint16_t)strtol(addrStr.c_str(), NULL, 0);

    def.name = regObj["name"] | "unnamed";
    def.fc = regObj["fc"] | 4;  // Default to FC 0x04 (Input Registers)
    def.type = regObj["type"] | "uint16";
    def.scale = regObj["scale"] | 1.0f;
    def.unit = regObj["unit"] | "";
    def.isStorage = regObj["storage"] | false;

    // Validate
    if (def.fc != 3 && def.fc != 4)
    {
        ESPLogger::warn("Invalid FC %d for register %s, skipping", def.fc, def.name.c_str());
        return false;
    }

    return true;
}

bool GenericModbusDevice::readBasic(std::vector<TelemetryPoint> &out)
{
    out.clear();

    if (!initialized)
    {
        ESPLogger::error("Device not initialized");
        return false;
    }

    bool success = true;
    int readCount = 0;

    for (const auto &reg : registers)
    {
        // Skip storage registers in basic read
        if (reg.isStorage)
            continue;

        float value;
        if (readRegister(reg, value))
        {
            out.push_back({reg.name, reg.unit, String(value, 2)});
            readCount++;
        }
        else
        {
            success = false;
        }
    }

    ESPLogger::info("Read %d/%d basic registers from %s", readCount, registers.size() - getStorageRegisterCount(), deviceName.c_str());
    return success;
}

bool GenericModbusDevice::readStorage(std::vector<TelemetryPoint> &out)
{
    out.clear();

    if (!initialized)
    {
        return false;
    }

    int readCount = 0;

    for (const auto &reg : registers)
    {
        // Only read storage registers
        if (!reg.isStorage)
            continue;

        float value;
        if (readRegister(reg, value))
        {
            out.push_back({reg.name, reg.unit, String(value, 2)});
            readCount++;
        }
    }

    if (readCount > 0)
    {
        ESPLogger::info("Read %d storage registers from %s", readCount, deviceName.c_str());
    }

    return readCount > 0;
}

bool GenericModbusDevice::readRegister(const RegisterDef &def, float &value)
{
    uint16_t data[2];
    uint8_t count = (def.type == "uint32" || def.type == "int32") ? 2 : 1;

    bool success = false;
    if (def.fc == 3)
    {
        success = modbus.readHoldingRegisters(slaveAddr, def.addr, count, data);
    }
    else if (def.fc == 4)
    {
        success = modbus.readInputRegisters(slaveAddr, def.addr, count, data);
    }

    if (!success)
    {
        ESPLogger::debug("Failed to read register %s (0x%04X)", def.name.c_str(), def.addr);
        return false;
    }

    // Parse value based on type
    if (def.type == "uint16")
    {
        value = data[0] * def.scale;
    }
    else if (def.type == "int16")
    {
        int16_t signed_val = (int16_t)data[0];
        value = signed_val * def.scale;
    }
    else if (def.type == "uint32")
    {
        uint32_t val32 = ((uint32_t)data[0] << 16) | data[1];
        value = val32 * def.scale;
    }
    else if (def.type == "int32")
    {
        int32_t signed_val32 = (int32_t)(((uint32_t)data[0] << 16) | data[1]);
        value = signed_val32 * def.scale;
    }
    else
    {
        ESPLogger::warn("Unknown type '%s' for register %s", def.type.c_str(), def.name.c_str());
        return false;
    }

    return true;
}

int GenericModbusDevice::getStorageRegisterCount() const
{
    int count = 0;
    for (const auto &reg : registers)
    {
        if (reg.isStorage)
            count++;
    }
    return count;
}
