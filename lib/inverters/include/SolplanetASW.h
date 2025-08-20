#pragma once

#include <Arduino.h>
#include <vector>
#include "InverterInterface.h"
#include "Config.h"
#include "ModbusClient.h"

class SolplanetASW : public InverterInterface
{
public:
    explicit SolplanetASW(ModbusClient &modbus, Config &config);
    bool begin() override;
    bool readBasic(std::vector<TelemetryPoint> &out) override;
    bool readStorage(std::vector<TelemetryPoint> &out) override;

private:
    ModbusClient &modbus;
    Config &config;
    uint8_t slaveAddr;
    bool isThreePhase;
    bool phaseDetected;

    // Helper methods
    uint16_t inputRegisterToModbus(uint16_t inputRegister) const;
    bool detectPhaseConfiguration();

    // Register reading methods
    bool readDeviceInfo(std::vector<TelemetryPoint> &out);
    bool readBasicMeasurements(std::vector<TelemetryPoint> &out);
    bool readPVInputs(std::vector<TelemetryPoint> &out);
    bool readGridMeasurements(std::vector<TelemetryPoint> &out);
    bool readTemperatures(std::vector<TelemetryPoint> &out);
    bool readBatteryData(std::vector<TelemetryPoint> &out);
    bool readThreePhaseMeasurements(std::vector<TelemetryPoint> &out);
};
