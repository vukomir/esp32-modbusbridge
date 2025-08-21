#include "DDS238Simulator.h"
#include "ModbusClient.h"
#include <ESPLogger.h>
#include <memory>

DDS238Simulator::DDS238Simulator(ModbusClient &modbus, Config &config)
    : modbus(modbus), config(config), slaveAddr(20)
{
    slaveAddr = config.getInt("rtu_addr", 20); // Default to 20 for simulator
}

bool DDS238Simulator::begin()
{
    ESPLogger::info("Initializing DDS238 Energy Meter Simulator, slave address: %d", slaveAddr);
    ESPLogger::info("Expected register map: 0x0000-0x0011 (Total: 18 registers)");
    ESPLogger::info("Device configuration: 9600 baud, 8N1, Function Code 03");
    
    if (!modbus.isInitialized())
    {
        ESPLogger::error("Modbus client not initialized");
        return false;
    }
    
    return true;
}

bool DDS238Simulator::readBasic(std::vector<TelemetryPoint> &out)
{
    out.clear();
    bool success = true;
    
    ESPLogger::info("Reading DDS238 simulator basic telemetry...");
    
    // Read energy registers first (these are the core functionality)
    if (!readEnergyRegisters(out))
    {
        ESPLogger::warn("Failed to read energy registers from DDS238 simulator");
        success = false;
    }
    
    // Read instantaneous measurement registers
    if (!readInstantaneousRegisters(out))
    {
        ESPLogger::warn("Failed to read instantaneous registers from DDS238 simulator");
        success = false;
    }
    
    ESPLogger::info("Read %d telemetry points from DDS238 simulator", out.size());
    return success && !out.empty();
}

bool DDS238Simulator::readStorage(std::vector<TelemetryPoint> &out)
{
    // Energy meters don't have storage/battery data
    out.clear();
    return true;
}

bool DDS238Simulator::readEnergyRegisters(std::vector<TelemetryPoint> &out)
{
    ESPLogger::debug("Reading energy registers from DDS238 simulator...");
    
    // Read Total Energy (0x0000-0x0001): 32-bit value, 0.01 kWh resolution
    uint16_t totalEnergyRegs[2];
    if (modbus.readHoldingRegisters(slaveAddr, 0x0000, 2, totalEnergyRegs))
    {
        uint32_t totalEnergy = combine32BitValue(totalEnergyRegs[0], totalEnergyRegs[1]);
        if (isValidRegisterValue32(totalEnergy))
        {
            float totalEnergyKWh = totalEnergy * 0.01f;
            out.push_back({"total_energy", "kWh", String(totalEnergyKWh, 2)});
            ESPLogger::info("Total Energy: %.2f kWh (raw: %lu)", totalEnergyKWh, totalEnergy);
        }
    }
    else
    {
        ESPLogger::error("Failed to read total energy registers 0x0000-0x0001");
        return false;
    }
    
    // Read Export Energy (0x0008-0x0009): 32-bit value, 0.01 kWh resolution
    uint16_t exportEnergyRegs[2];
    if (modbus.readHoldingRegisters(slaveAddr, 0x0008, 2, exportEnergyRegs))
    {
        uint32_t exportEnergy = combine32BitValue(exportEnergyRegs[0], exportEnergyRegs[1]);
        if (isValidRegisterValue32(exportEnergy))
        {
            float exportEnergyKWh = exportEnergy * 0.01f;
            out.push_back({"export_energy", "kWh", String(exportEnergyKWh, 2)});
            ESPLogger::debug("Export Energy: %.2f kWh", exportEnergyKWh);
        }
    }
    else
    {
        ESPLogger::debug("Export energy registers 0x0008-0x0009 not available (optional)");
    }
    
    // Read Import Energy (0x000A-0x000B): 32-bit value, 0.01 kWh resolution
    uint16_t importEnergyRegs[2];
    if (modbus.readHoldingRegisters(slaveAddr, 0x000A, 2, importEnergyRegs))
    {
        uint32_t importEnergy = combine32BitValue(importEnergyRegs[0], importEnergyRegs[1]);
        if (isValidRegisterValue32(importEnergy))
        {
            float importEnergyKWh = importEnergy * 0.01f;
            out.push_back({"import_energy", "kWh", String(importEnergyKWh, 2)});
            ESPLogger::debug("Import Energy: %.2f kWh", importEnergyKWh);
        }
    }
    else
    {
        ESPLogger::debug("Import energy registers 0x000A-0x000B not available (optional)");
    }
    
    return true;
}

bool DDS238Simulator::readInstantaneousRegisters(std::vector<TelemetryPoint> &out)
{
    ESPLogger::debug("Reading instantaneous registers from DDS238 simulator...");
    
    // Read instantaneous values (0x000C-0x0011): 6 registers
    uint16_t instantaneous[6];
    if (modbus.readHoldingRegisters(slaveAddr, 0x000C, 6, instantaneous))
    {
        // Voltage (0x000C): uint16, 0.1 V resolution
        uint16_t voltageRaw = instantaneous[0];
        if (isValidRegisterValue(voltageRaw))
        {
            float voltage = voltageRaw * 0.1f;
            out.push_back({"voltage", "V", String(voltage, 1)});
            ESPLogger::debug("Voltage: %.1f V", voltage);
        }
        
        // Current (0x000D): uint16, 0.01 A resolution
        uint16_t currentRaw = instantaneous[1];
        if (isValidRegisterValue(currentRaw))
        {
            float current = currentRaw * 0.01f;
            out.push_back({"current", "A", String(current, 2)});
            ESPLogger::debug("Current: %.2f A", current);
        }
        
        // Active Power (0x000E): int16, 1 W resolution
        int16_t activePower = (int16_t)instantaneous[2];
        if (activePower != (int16_t)0x8000) // Check for invalid signed value
        {
            out.push_back({"active_power", "W", String(activePower)});
            ESPLogger::debug("Active Power: %d W", activePower);
        }
        
        // Reactive Power (0x000F): int16, 1 VAR resolution
        int16_t reactivePower = (int16_t)instantaneous[3];
        if (reactivePower != (int16_t)0x8000)
        {
            out.push_back({"reactive_power", "VAR", String(reactivePower)});
            ESPLogger::debug("Reactive Power: %d VAR", reactivePower);
        }
        
        // Power Factor (0x0010): uint16, 0.001 resolution
        uint16_t powerFactorRaw = instantaneous[4];
        if (isValidRegisterValue(powerFactorRaw))
        {
            float powerFactor = powerFactorRaw * 0.001f;
            out.push_back({"power_factor", "", String(powerFactor, 3)});
            ESPLogger::debug("Power Factor: %.3f", powerFactor);
        }
        
        // Frequency (0x0011): uint16, 0.01 Hz resolution
        uint16_t frequencyRaw = instantaneous[5];
        if (isValidRegisterValue(frequencyRaw))
        {
            float frequency = frequencyRaw * 0.01f;
            out.push_back({"frequency", "Hz", String(frequency, 2)});
            ESPLogger::debug("Frequency: %.2f Hz", frequency);
        }
        
        ESPLogger::info("Successfully read instantaneous values from DDS238 simulator");
        return true;
    }
    else
    {
        ESPLogger::error("Failed to read instantaneous values from registers 0x000C-0x0011");
        return false;
    }
}

uint32_t DDS238Simulator::combine32BitValue(uint16_t low, uint16_t high) const
{
    // Combine low and high 16-bit registers into 32-bit value
    return ((uint32_t)high << 16) | low;
}

bool DDS238Simulator::isValidRegisterValue(uint16_t value) const
{
    // Check for common invalid values
    return value != 0xFFFF && value != 0x8000;
}

bool DDS238Simulator::isValidRegisterValue32(uint32_t value) const
{
    // Check for common invalid 32-bit values
    return value != 0xFFFFFFFF && value != 0x80000000;
}
