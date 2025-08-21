#pragma once

#include "InverterInterface.h"
#include "ModbusClient.h"
#include "Config.h"
#include <vector>

/**
 * @brief ESP32 RS485 Energy Meter Simulator implementation
 * 
 * Implements the exact register map for the DDS238 simulator:
 * - Slave ID: 20 (configurable)
 * - Baud Rate: 9600, 8N1
 * - Function Code: 03 (Read Holding Registers)
 * 
 * Register Map:
 * 0x0000-0x0001: Total Energy (32-bit, 0.01 kWh resolution)
 * 0x0008-0x0009: Export Energy (32-bit, 0.01 kWh resolution)  
 * 0x000A-0x000B: Import Energy (32-bit, 0.01 kWh resolution)
 * 0x000C: Voltage (16-bit, 0.1 V resolution)
 * 0x000D: Current (16-bit, 0.01 A resolution)
 * 0x000E: Active Power (16-bit signed, 1 W resolution)
 * 0x000F: Reactive Power (16-bit signed, 1 VAR resolution)
 * 0x0010: Power Factor (16-bit, 0.001 resolution)
 * 0x0011: Frequency (16-bit, 0.01 Hz resolution)
 */
class DDS238Simulator : public InverterInterface
{
public:
    DDS238Simulator(ModbusClient &modbus, Config &config);
    
    bool begin() override;
    bool readBasic(std::vector<TelemetryPoint> &out) override;
    bool readStorage(std::vector<TelemetryPoint> &out) override;

private:
    ModbusClient &modbus;
    Config &config;
    uint8_t slaveAddr;
    
    // Register reading methods
    bool readEnergyRegisters(std::vector<TelemetryPoint> &out);
    bool readInstantaneousRegisters(std::vector<TelemetryPoint> &out);
    
    // Utility methods
    uint32_t combine32BitValue(uint16_t low, uint16_t high) const;
    bool isValidRegisterValue(uint16_t value) const;
    bool isValidRegisterValue32(uint32_t value) const;
};
