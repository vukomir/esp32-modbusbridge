#pragma once

#include <Arduino.h>
#include <vector>
#include "InverterInterface.h"
#include "Config.h"
#include "ModbusClient.h"

class HikingDDS238 : public InverterInterface
{
public:
    explicit HikingDDS238(ModbusClient &modbus, Config &config);
    bool begin() override;
    bool readBasic(std::vector<TelemetryPoint> &out) override;
    bool readStorage(std::vector<TelemetryPoint> &out) override;

private:
    ModbusClient &modbus;
    Config &config;
    uint8_t slaveAddr;

    // Helper methods for reading different register groups
    bool readInstantaneousValues(std::vector<TelemetryPoint> &out);
    bool readEnergyValues(std::vector<TelemetryPoint> &out);
};
