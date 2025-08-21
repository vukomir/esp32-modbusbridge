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

    ESPLogger::info("Reading DDS238 simulator basic telemetry (optimized multi-register)...");

    // Try to read ALL registers in one operation (0x0000-0x0011 = 18 registers)
    // This is the most efficient approach - single Modbus transaction
    uint16_t allRegisters[18];
    if (modbus.readHoldingRegisters(slaveAddr, 0x0000, 18, allRegisters))
    {
        ESPLogger::info("✅ Successfully read all 18 registers in single operation!");

        // Parse energy registers (0x0000-0x000B)
        parseEnergyFromBlock(allRegisters, out);

        // Parse instantaneous registers (0x000C-0x0011) - offset by 12 in the block
        parseInstantaneousFromBlock(&allRegisters[12], out);

        success = true;
    }
    else
    {
        ESPLogger::debug("Full register block read failed, trying separate operations...");

        // Fallback to separate energy and instantaneous reads
        if (!readEnergyRegisters(out))
        {
            ESPLogger::debug("Energy registers not available from DDS238 simulator (optional)");
        }

        // Read instantaneous measurement registers (required)
        if (!readInstantaneousRegisters(out))
        {
            ESPLogger::warn("Failed to read instantaneous registers from DDS238 simulator");
            success = false;
        }
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
    ESPLogger::debug("Reading energy registers from DDS238 simulator (multi-register optimization)...");

    // Try to read all energy registers in one operation (0x0000-0x000B = 12 registers)
    // This is more efficient than 3 separate 2-register reads
    uint16_t energyBlock[12];
    if (modbus.readHoldingRegisters(slaveAddr, 0x0000, 12, energyBlock))
    {
        ESPLogger::debug("Successfully read energy block (12 registers) in single operation");

        // Parse Total Energy (0x0000-0x0001)
        uint32_t totalEnergy = combine32BitValue(energyBlock[0], energyBlock[1]);
        if (isValidRegisterValue32(totalEnergy))
        {
            float totalEnergyKWh = totalEnergy * 0.01f;
            out.push_back({"total_energy", "kWh", String(totalEnergyKWh, 2)});
            ESPLogger::info("Total Energy: %.2f kWh (raw: %lu)", totalEnergyKWh, totalEnergy);
        }

        // Parse Export Energy (0x0008-0x0009) - registers 8-9 in block
        uint32_t exportEnergy = combine32BitValue(energyBlock[8], energyBlock[9]);
        if (isValidRegisterValue32(exportEnergy))
        {
            float exportEnergyKWh = exportEnergy * 0.01f;
            out.push_back({"export_energy", "kWh", String(exportEnergyKWh, 2)});
            ESPLogger::debug("Export Energy: %.2f kWh", exportEnergyKWh);
        }

        // Parse Import Energy (0x000A-0x000B) - registers 10-11 in block
        uint32_t importEnergy = combine32BitValue(energyBlock[10], energyBlock[11]);
        if (isValidRegisterValue32(importEnergy))
        {
            float importEnergyKWh = importEnergy * 0.01f;
            out.push_back({"import_energy", "kWh", String(importEnergyKWh, 2)});
            ESPLogger::debug("Import Energy: %.2f kWh", importEnergyKWh);
        }

        return true;
    }
    else
    {
        ESPLogger::debug("Energy block read failed, trying individual register pairs...");

        // Fallback to individual 2-register reads if block read fails
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
            ESPLogger::debug("Total energy registers 0x0000-0x0001 not available (optional for simulators)");
        }

        // Skip export/import energy if total energy failed - likely not implemented
        return out.size() > 0; // Return true if we got any energy data
    }
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

void DDS238Simulator::parseEnergyFromBlock(uint16_t *block, std::vector<TelemetryPoint> &out)
{
    // Parse Total Energy (registers 0-1 in block)
    uint32_t totalEnergy = combine32BitValue(block[0], block[1]);
    if (isValidRegisterValue32(totalEnergy))
    {
        float totalEnergyKWh = totalEnergy * 0.01f;
        out.push_back({"total_energy", "kWh", String(totalEnergyKWh, 2)});
        ESPLogger::debug("Total Energy: %.2f kWh", totalEnergyKWh);
    }

    // Parse Export Energy (registers 8-9 in block)
    uint32_t exportEnergy = combine32BitValue(block[8], block[9]);
    if (isValidRegisterValue32(exportEnergy))
    {
        float exportEnergyKWh = exportEnergy * 0.01f;
        out.push_back({"export_energy", "kWh", String(exportEnergyKWh, 2)});
        ESPLogger::debug("Export Energy: %.2f kWh", exportEnergyKWh);
    }

    // Parse Import Energy (registers 10-11 in block)
    uint32_t importEnergy = combine32BitValue(block[10], block[11]);
    if (isValidRegisterValue32(importEnergy))
    {
        float importEnergyKWh = importEnergy * 0.01f;
        out.push_back({"import_energy", "kWh", String(importEnergyKWh, 2)});
        ESPLogger::debug("Import Energy: %.2f kWh", importEnergyKWh);
    }
}

void DDS238Simulator::parseInstantaneousFromBlock(uint16_t *block, std::vector<TelemetryPoint> &out)
{
    // Parse Voltage (register 0 in instantaneous block = 0x000C)
    uint16_t voltageRaw = block[0];
    if (isValidRegisterValue(voltageRaw))
    {
        float voltage = voltageRaw * 0.1f;
        out.push_back({"voltage", "V", String(voltage, 1)});
        ESPLogger::debug("Voltage: %.1f V", voltage);
    }

    // Parse Current (register 1 in instantaneous block = 0x000D)
    uint16_t currentRaw = block[1];
    if (isValidRegisterValue(currentRaw))
    {
        float current = currentRaw * 0.01f;
        out.push_back({"current", "A", String(current, 2)});
        ESPLogger::debug("Current: %.2f A", current);
    }

    // Parse Active Power (register 2 in instantaneous block = 0x000E)
    int16_t activePower = (int16_t)block[2];
    if (activePower != (int16_t)0x8000)
    {
        out.push_back({"active_power", "W", String(activePower)});
        ESPLogger::debug("Active Power: %d W", activePower);
    }

    // Parse Reactive Power (register 3 in instantaneous block = 0x000F)
    int16_t reactivePower = (int16_t)block[3];
    if (reactivePower != (int16_t)0x8000)
    {
        out.push_back({"reactive_power", "VAR", String(reactivePower)});
        ESPLogger::debug("Reactive Power: %d VAR", reactivePower);
    }

    // Parse Power Factor (register 4 in instantaneous block = 0x0010)
    uint16_t powerFactorRaw = block[4];
    if (isValidRegisterValue(powerFactorRaw))
    {
        float powerFactor = powerFactorRaw * 0.001f;
        out.push_back({"power_factor", "", String(powerFactor, 3)});
        ESPLogger::debug("Power Factor: %.3f", powerFactor);
    }

    // Parse Frequency (register 5 in instantaneous block = 0x0011)
    uint16_t frequencyRaw = block[5];
    if (isValidRegisterValue(frequencyRaw))
    {
        float frequency = frequencyRaw * 0.01f;
        out.push_back({"frequency", "Hz", String(frequency, 2)});
        ESPLogger::debug("Frequency: %.2f Hz", frequency);
    }
}
