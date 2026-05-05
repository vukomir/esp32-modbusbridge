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
    uint8_t mpptCount;      // Number of MPPT/PV inputs (from register 31074)
    uint8_t stringCount;    // Number of individual strings (from register 31075)
    bool configDetected;    // True after reading device configuration

    // Helper methods
    uint16_t inputRegisterToModbus(uint16_t inputRegister) const;
    bool detectPhaseConfiguration();
    bool detectDeviceConfiguration(); // Reads MPPT count, string count, etc.

    // NaN value checking (per MB001 spec)
    inline bool isNaN_U16(uint16_t val) const { return val == 0xFFFF; }
    inline bool isNaN_S16(int16_t val) const { return val == (int16_t)0x8000; }
    inline bool isNaN_U32(uint32_t val) const { return val == 0xFFFFFFFF; }
    inline bool isNaN_S32(int32_t val) const { return val == (int32_t)0x80000000; }

    // Register reading methods (optimized with bulk reads)
    bool readDeviceInfo(std::vector<TelemetryPoint> &out);
    bool readBasicMeasurements(std::vector<TelemetryPoint> &out);          // Bulk: 31301-31320
    bool readPVInputs(std::vector<TelemetryPoint> &out);                   // Bulk: 31319-31338, 31339-31358
    bool readGridAndPhaseMeasurements(std::vector<TelemetryPoint> &out);   // Bulk: 31359-31379 (includes error/warning codes)
    bool readGridEnergyAndLoad(std::vector<TelemetryPoint> &out);          // Bulk: 31630-31678 (consumption, grid export, EPS load)

    // Code translation (MB001 sections 3.4, 3.5)
    String getErrorDescription(uint16_t errorCode) const;
    String getWarningDescription(uint16_t warningCode) const;
    String getGridCodeDescription(uint16_t gridCode) const;
};
