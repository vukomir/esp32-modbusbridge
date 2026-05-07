#include "InverterFactory.h"
#include "SolplanetASW.h"
#include "HikingDDS238.h"
#include "DDS238Simulator.h"
#include "GenericModbusDevice.h"
#include "ModbusClient.h"
#include "constants.h"
#include <memory>

std::unique_ptr<InverterInterface> InverterFactory::create(const String &model, ModbusClient &modbus, Config &config)
{
    // Check if this is a JSON-based device (format: "json:filename.json")
    if (model.startsWith("json:"))
    {
        String filename = model.substring(5);
        // Defense-in-depth: WebUI validates filenames at upload time, but
        // device_model can be written to config via other paths (factory reset
        // recovery, manual config edit, future API). Re-validate before we
        // build a LittleFS path with it. Otherwise "json:../config.json"
        // would point the device parser at /config.json.
        if (!GenericModbusDevice::isSafeDeviceFilename(filename))
        {
            return nullptr;
        }
        String jsonFile = "/devices/" + filename;
        return std::unique_ptr<InverterInterface>(new GenericModbusDevice(modbus, config, jsonFile));
    }

    // Builtin C++ device classes
    if (model == SOLPLANET_ASW_MODEL || model == "solplanet_asw_hybrid")
    {
        // Both GEN and HYBRID series use the same SolplanetASW class
        // Phase configuration is auto-detected from register 31001
        // Backward compatibility: old "solplanet_asw_hybrid" configs still work
        return std::unique_ptr<InverterInterface>(new SolplanetASW(modbus, config));
    }
    else if (model == HIKING_DDS238_MODEL)
    {
        return std::unique_ptr<InverterInterface>(new HikingDDS238(modbus, config));
    }
    else if (model == DDS238_SIMULATOR_MODEL)
    {
        return std::unique_ptr<InverterInterface>(new DDS238Simulator(modbus, config));
    }
    return nullptr;
}

String InverterFactory::getDeviceType(const String &model)
{
    // JSON devices are type "custom"
    if (model.startsWith("json:"))
    {
        return "custom";
    }

    for (size_t i = 0; i < SUPPORTED_DEVICES_COUNT; i++)
    {
        if (model == SUPPORTED_DEVICES[i].model)
        {
            return String(SUPPORTED_DEVICES[i].type);
        }
    }
    return "unknown";
}
