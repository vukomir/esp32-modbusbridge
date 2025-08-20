#pragma once

#include <Arduino.h>
#include <memory>
#include "InverterInterface.h"

// Forward declarations
class ModbusClient;
class Config;

class InverterFactory
{
public:
    static std::unique_ptr<InverterInterface> create(const String &model, ModbusClient &modbus, Config &config);
    static String getDeviceType(const String &model);
};
