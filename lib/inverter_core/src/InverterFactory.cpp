#include "InverterFactory.h"
#include "SolplanetASW.h"
#include "HikingDDS238.h"
#include "ModbusClient.h"
#include "constants.h"
#include <memory>

std::unique_ptr<InverterInterface> InverterFactory::create(const String &model, ModbusClient &modbus, Config &config)
{
    if (model == SOLPLANET_ASW_MODEL)
    {
        return std::unique_ptr<InverterInterface>(new SolplanetASW(modbus, config));
    }
    else if (model == HIKING_DDS238_MODEL)
    {
        return std::unique_ptr<InverterInterface>(new HikingDDS238(modbus, config));
    }
    return nullptr;
}

String InverterFactory::getDeviceType(const String &model)
{
    for (size_t i = 0; i < SUPPORTED_DEVICES_COUNT; i++)
    {
        if (model == SUPPORTED_DEVICES[i].model)
        {
            return String(SUPPORTED_DEVICES[i].type);
        }
    }
    return "unknown";
}
