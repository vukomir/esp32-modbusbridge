#include "HikingDDS238.h"
#include "ModbusClient.h"
#include <ESPLogger.h>
#include <memory>

HikingDDS238::HikingDDS238(ModbusClient &modbus, Config &config)
    : modbus(modbus), config(config), slaveAddr(1)
{
    slaveAddr = config.getInt("rtu_addr", 1);
}

bool HikingDDS238::begin()
{
    ESPLogger::info("Initializing Hiking DDS238 smart meter, slave address: %d", slaveAddr);
    return modbus.isInitialized();
}

bool HikingDDS238::readBasic(std::vector<TelemetryPoint> &out)
{
    out.clear();
    bool success = true;

    // Read instantaneous values (voltage, current, power, etc.)
    if (!readInstantaneousValues(out))
    {
        ESPLogger::error("Failed to read instantaneous values from Hiking DDS238");
        success = false;
    }

    // Read energy values (total, import, export)
    if (!readEnergyValues(out))
    {
        ESPLogger::error("Failed to read energy values from Hiking DDS238");
        success = false;
    }

    ESPLogger::info("Read %d telemetry points from Hiking DDS238", out.size());
    return success && !out.empty();
}

bool HikingDDS238::readStorage(std::vector<TelemetryPoint> &out)
{
    // Smart meters typically don't have separate "storage" readings
    // This is mainly for solar inverters with battery storage
    out.clear();
    return true;
}

bool HikingDDS238::readInstantaneousValues(std::vector<TelemetryPoint> &out)
{
    // Read registers 0x000C to 0x0011 (voltage, current, power, power factor, frequency)
    uint16_t instantaneous[6];
    if (!modbus.readHoldingRegisters(slaveAddr, 0x000C, 6, instantaneous))
    {
        ESPLogger::error("Failed to read instantaneous values from registers 0x000C-0x0011");
        return false;
    }

    // Voltage (0x000C): U_WORD, multiply by 0.1
    uint16_t voltage = instantaneous[0];
    if (voltage != 0xFFFF)
    {
        out.push_back({"voltage", "V", String(voltage * 0.1f, 1)});
    }

    // Current (0x000D): U_WORD, multiply by 0.01
    uint16_t current = instantaneous[1];
    if (current != 0xFFFF)
    {
        out.push_back({"current", "A", String(current * 0.01f, 2)});
    }

    // Power (0x000E): S_WORD (signed), no scaling
    int16_t power = (int16_t)instantaneous[2];
    if (power != (int16_t)0xFFFF)
    {
        out.push_back({"power", "W", String(power)});
    }

    // Apparent Power (0x000F): U_WORD, no scaling
    uint16_t apparentPower = instantaneous[3];
    if (apparentPower != 0xFFFF)
    {
        out.push_back({"apparent_power", "VA", String(apparentPower)});
    }

    // Power Factor (0x0010): U_WORD, multiply by 0.001
    uint16_t powerFactor = instantaneous[4];
    if (powerFactor != 0xFFFF)
    {
        out.push_back({"power_factor", "", String(powerFactor * 0.001f, 3)});
    }

    // Frequency (0x0011): U_WORD, multiply by 0.01
    uint16_t frequency = instantaneous[5];
    if (frequency != 0xFFFF)
    {
        out.push_back({"frequency", "Hz", String(frequency * 0.01f, 2)});
    }

    return true;
}

bool HikingDDS238::readEnergyValues(std::vector<TelemetryPoint> &out)
{
    // Read Energy Total (0x0000-0x0001): U_DWORD, multiply by 0.01
    uint16_t energyTotal[2];
    if (modbus.readHoldingRegisters(slaveAddr, 0x0000, 2, energyTotal))
    {
        uint32_t totalEnergy = modbus.combineRegisters(energyTotal[0], energyTotal[1]);
        if (totalEnergy != 0xFFFFFFFF)
        {
            out.push_back({"energy_total", "kWh", String(totalEnergy * 0.01f, 2)});
        }
    }
    else
    {
        ESPLogger::error("Failed to read energy total from registers 0x0000-0x0001");
        return false;
    }

    // Read Energy Export (0x0008-0x0009): U_DWORD, multiply by 0.01
    uint16_t energyExport[2];
    if (modbus.readHoldingRegisters(slaveAddr, 0x0008, 2, energyExport))
    {
        uint32_t exportEnergy = modbus.combineRegisters(energyExport[0], energyExport[1]);
        if (exportEnergy != 0xFFFFFFFF)
        {
            out.push_back({"energy_export", "kWh", String(exportEnergy * 0.01f, 2)});
        }
    }
    else
    {
        ESPLogger::error("Failed to read energy export from registers 0x0008-0x0009");
        return false;
    }

    // Read Energy Import (0x000A-0x000B): U_DWORD, multiply by 0.01
    uint16_t energyImport[2];
    if (modbus.readHoldingRegisters(slaveAddr, 0x000A, 2, energyImport))
    {
        uint32_t importEnergy = modbus.combineRegisters(energyImport[0], energyImport[1]);
        if (importEnergy != 0xFFFFFFFF)
        {
            out.push_back({"energy_import", "kWh", String(importEnergy * 0.01f, 2)});
        }
    }
    else
    {
        ESPLogger::error("Failed to read energy import from registers 0x000A-0x000B");
        return false;
    }

    return true;
}
