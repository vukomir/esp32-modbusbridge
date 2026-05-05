#include "SolplanetASW.h"
#include "ModbusClient.h"
#include "constants.h"
#include <ESPLogger.h>

// Store large constant strings in PROGMEM to save RAM

// Helper macro to create TelemetryPoint with full metadata
#define TP_FULL(name, unit, value, reg, type, scale) \
    TelemetryPoint(name, unit, value, reg, type, scale)

SolplanetASW::SolplanetASW(ModbusClient &modbus, Config &config)
    : modbus(modbus), config(config), slaveAddr(1), isThreePhase(false), phaseDetected(false),
      mpptCount(0), stringCount(0), configDetected(false)
{
    slaveAddr = config.getInt("rtu_addr", 1);
}

bool SolplanetASW::begin()
{
    ESPLogger::info("Initializing SolPlanet ASW inverter (GEN/HYBRID), slave address: %d", slaveAddr);

    if (!modbus.isInitialized())
    {
        ESPLogger::error("Modbus client not initialized");
        return false;
    }

    // Detect phase configuration
    if (!detectPhaseConfiguration())
    {
        ESPLogger::warn("Failed to detect phase configuration, will retry on first read");
    }

    return true;
}

bool SolplanetASW::detectPhaseConfiguration()
{
    if (phaseDetected)
    {
        return true;
    }

    // Auto-detect phase configuration by reading Device Type register (31001)
    // According to MB001_ASW GEN-Modbus-en_V2.1.7.pdf page 5:
    // Register 31001: Device Type String
    // Returns: ASCII '1' (49) = Single phase, ASCII '3' (51) = Three phase

    ESPLogger::info("Auto-detecting phase configuration from register 31001...");

    uint16_t deviceType;
    if (modbus.readInputRegisters(slaveAddr, inputRegisterToModbus(31001), 1, &deviceType))
    {
        // Check ASCII value (the register stores '1' or '3' as ASCII)
        if (deviceType == 49 || deviceType == '1')  // ASCII '1' = single phase
        {
            isThreePhase = false;
            ESPLogger::info("✓ Detected 1-phase inverter (device type: %u / '%c')", deviceType, (char)deviceType);
        }
        else if (deviceType == 51 || deviceType == '3')  // ASCII '3' = three phase
        {
            isThreePhase = true;
            ESPLogger::info("✓ Detected 3-phase inverter (device type: %u / '%c')", deviceType, (char)deviceType);
        }
        else
        {
            // Unknown value - log warning but DON'T latch detection as successful
            ESPLogger::warn("Unknown device type value: %u (0x%04X), cannot determine phase configuration", deviceType, deviceType);
            return false;
        }

        phaseDetected = true;
        return true;
    }
    else
    {
        ESPLogger::error("Failed to read device type register 31001");
        ESPLogger::error("Check: RS485 wiring, slave address (%d), baud rate (9600)", slaveAddr);
        return false;
    }
}

bool SolplanetASW::detectDeviceConfiguration()
{
    if (configDetected)
    {
        return true;
    }

    ESPLogger::info("Detecting device configuration (MPPT & string counts)...");

    bool success = true;

    // Read register 31074: MPPT number (U16)
    // This tells us how many PV/MPPT inputs the inverter has (typically 2-4)
    uint16_t mpptNum;
    if (modbus.readInputRegisters(slaveAddr, inputRegisterToModbus(31074), 1, &mpptNum))
    {
        mpptCount = mpptNum;
        ESPLogger::info("✓ Detected %u MPPT/PV inputs", mpptCount);

        // Validate reasonable range (ASW models have 2-4 MPPT typically)
        if (mpptCount == 0 || mpptCount > 10)
        {
            ESPLogger::warn("Unusual MPPT count: %u, defaulting to 2", mpptCount);
            mpptCount = 2;
        }
    }
    else
    {
        ESPLogger::warn("Failed to read MPPT count (register 31074), defaulting to 2");
        mpptCount = 2;
        success = false;
    }

    // Read register 31075: String current number (U16)
    // This tells us how many individual string currents to read (registers 31339-31358)
    uint16_t stringNum;
    if (modbus.readInputRegisters(slaveAddr, inputRegisterToModbus(31075), 1, &stringNum))
    {
        stringCount = stringNum;
        ESPLogger::info("✓ Detected %u individual strings", stringCount);

        // Validate reasonable range (up to 20 strings per PDF)
        if (stringCount > 20)
        {
            ESPLogger::warn("String count exceeds 20: %u, capping at 20", stringCount);
            stringCount = 20;
        }
    }
    else
    {
        ESPLogger::info("No individual string currents detected (register 31075)");
        stringCount = 0;
        // Not a failure - some models don't have separate string currents
    }

    configDetected = true;
    return success;
}

uint16_t SolplanetASW::inputRegisterToModbus(uint16_t inputRegister) const
{
    // Convert 3xxxx input register number to 0-based Modbus address
    // Per MB001 doc page 3: "31001 (decimal) → 1000 (decimal) → 0x03e8"
    return inputRegister - 30001;
}

bool SolplanetASW::readBasic(std::vector<TelemetryPoint> &out)
{
    out.clear();

    // Ensure phase configuration is detected
    if (!phaseDetected && !detectPhaseConfiguration())
    {
        ESPLogger::error("Cannot read telemetry without phase detection");
        return false;
    }

    // Ensure device configuration is detected (MPPT count, etc.)
    if (!configDetected)
    {
        detectDeviceConfiguration(); // Non-fatal if it fails, uses defaults
    }

    bool success = true;

    // Read device information
    if (!readDeviceInfo(out))
    {
        ESPLogger::warn("Failed to read device info");
        success = false;
    }

    // Read basic measurements (energy, state, temperatures)
    if (!readBasicMeasurements(out))
    {
        ESPLogger::warn("Failed to read basic measurements");
        success = false;
    }

    // Read PV input data
    if (!readPVInputs(out))
    {
        ESPLogger::warn("Failed to read PV inputs");
        success = false;
    }

    // Read grid/AC measurements and 3-phase data (combined bulk read)
    if (!readGridAndPhaseMeasurements(out))
    {
        ESPLogger::warn("Failed to read grid/phase measurements");
        success = false;
    }

    // Read grid energy, consumption, and EPS load data
    if (!readGridEnergyAndLoad(out))
    {
        ESPLogger::warn("Failed to read grid energy/load data");
        success = false;
    }

    // Note: Temperatures are now included in readBasicMeasurements bulk read

    ESPLogger::info("Read %d telemetry points (%s)", out.size(), isThreePhase ? "3-phase" : "1-phase");
    return success && !out.empty();
}

bool SolplanetASW::readDeviceInfo(std::vector<TelemetryPoint> &out)
{
    // BULK READ OPTIMIZATION: Read 2 separate single registers
    // These are non-contiguous so we keep them as individual reads

    // Register 31002: Modbus address (U16)
    uint16_t modbusAddr;
    if (modbus.readInputRegisters(slaveAddr, inputRegisterToModbus(31002), 1, &modbusAddr))
    {
        if (!isNaN_U16(modbusAddr))
        {
            out.push_back(TP_FULL("modbus_address", "", String(modbusAddr), 31002, "U16", 1.0f));
            ESPLogger::debug("Modbus address: %u", modbusAddr);
        }
    }

    // Register 31027: Current grid code (E16)
    uint16_t gridCode;
    if (modbus.readInputRegisters(slaveAddr, inputRegisterToModbus(31027), 1, &gridCode))
    {
        if (!isNaN_U16(gridCode))
        {
            out.push_back(TP_FULL("grid_code", "", String(gridCode), 31027, "E16", 1.0f));

            // Add human-readable grid code description
            String gridDesc = getGridCodeDescription(gridCode);
            if (gridDesc.length() > 0)
            {
                out.push_back(TP_FULL("grid_code_standard", "", gridDesc, 31027, "E16", 1.0f));
            }
        }
    }

    return true;
}

bool SolplanetASW::readBasicMeasurements(std::vector<TelemetryPoint> &out)
{
    // BULK READ OPTIMIZATION: Read 31301-31320 in one transaction (20 registers)
    // Covers: grid voltage/freq, energy, hours, state, connect time, temperatures, bus voltage, PV1
    uint16_t bulk[20];
    if (!modbus.readInputRegisters(slaveAddr, inputRegisterToModbus(31301), 20, bulk))
    {
        ESPLogger::error("Bulk read 31301-31320 failed");
        return false;
    }

    // Index 0: 31301 - Grid rated voltage (U16, V, scale 0.1)
    if (!isNaN_U16(bulk[0]))
    {
        float voltage = bulk[0] * 0.1f;
        out.push_back(TP_FULL("grid_rated_voltage", "V", String(voltage, 1), 31301, "U16", 0.1f));
    }

    // Index 1: 31302 - Grid rated frequency (U16, Hz, scale 0.01)
    if (!isNaN_U16(bulk[1]))
    {
        float freq = bulk[1] * 0.01f;
        out.push_back(TP_FULL("grid_rated_frequency", "Hz", String(freq, 2), 31302, "U16", 0.01f));
    }

    // Index 2-3: 31303-31304 - Today's energy (U32, kWh, scale 0.1)
    uint32_t todayEnergy = ((uint32_t)bulk[2] << 16) | bulk[3];
    if (!isNaN_U32(todayEnergy))
    {
        float energyKwh = todayEnergy * 0.1f;
        out.push_back(TP_FULL("energy_today", "kWh", String(energyKwh, 1), 31303, "U32", 0.1f));
    }

    // Index 4-5: 31305-31306 - Total energy (U32, kWh, scale 0.1)
    uint32_t totalEnergy = ((uint32_t)bulk[4] << 16) | bulk[5];
    if (!isNaN_U32(totalEnergy))
    {
        float energyKwh = totalEnergy * 0.1f;
        out.push_back(TP_FULL("energy_total", "kWh", String(energyKwh, 1), 31305, "U32", 0.1f));
    }

    // Index 6-7: 31307-31308 - Total hours (U32, hours, scale 1.0)
    uint32_t hours = ((uint32_t)bulk[6] << 16) | bulk[7];
    if (!isNaN_U32(hours))
    {
        out.push_back(TP_FULL("hours_total", "h", String(hours), 31307, "U32", 1.0f));
    }

    // Index 8: 31309 - Device state (E16)
    if (!isNaN_U16(bulk[8]))
    {
        uint16_t deviceState = bulk[8];
        out.push_back(TP_FULL("device_state", "", String(deviceState), 31309, "E16", 1.0f));
        String stateStr;
        switch (deviceState)
        {
        case 0: stateStr = "Wait"; break;
        case 1: stateStr = "Normal"; break;
        case 2: stateStr = "Fault"; break;
        case 4: stateStr = "Checking"; break;
        case 30: stateStr = "Dispatch"; break;
        case 40: stateStr = "Unknown40"; break;
        case 50: stateStr = "Over-Temp"; break;
        case 99: stateStr = "Stop"; break;
        case 170: stateStr = "Flash-Init"; break;
        default: stateStr = String(deviceState);
        }
        out.push_back(TP_FULL("device_state_text", "", stateStr, 31309, "E16", 1.0f));
    }

    // Index 9: 31310 - Connect time (U16, seconds)
    if (!isNaN_U16(bulk[9]))
    {
        out.push_back(TP_FULL("connect_time", "s", String(bulk[9]), 31310, "U16", 1.0f));
    }

    // Index 10: 31311 - Internal temperature (S16, °C, scale 0.1)
    int16_t tempInternal = (int16_t)bulk[10];
    if (!isNaN_S16(tempInternal))
    {
        float temperature = tempInternal * 0.1f;
        out.push_back(TP_FULL("temperature_internal", "C", String(temperature, 1), 31311, "S16", 0.1f));
    }

    // Index 11: 31312 - Inverter U phase temperature (S16, °C, scale 0.1)
    int16_t tempInverterU = (int16_t)bulk[11];
    if (!isNaN_S16(tempInverterU))
    {
        float temperature = tempInverterU * 0.1f;
        out.push_back(TP_FULL("temperature_inverter_u", "C", String(temperature, 1), 31312, "S16", 0.1f));
    }

    // Index 12-13: 31313-31314 - Unused/gaps, skip

    // Index 14: 31315 - Boost temperature (S16, °C, scale 0.1)
    int16_t tempBoost = (int16_t)bulk[14];
    if (!isNaN_S16(tempBoost))
    {
        float temperature = tempBoost * 0.1f;
        out.push_back(TP_FULL("temperature_boost", "C", String(temperature, 1), 31315, "S16", 0.1f));
    }

    // Index 15: 31316 - Unused/gap, skip

    // Index 16: 31317 - Bus voltage (U16, V, scale 0.1)
    if (!isNaN_U16(bulk[16]))
    {
        float voltage = bulk[16] * 0.1f;
        out.push_back(TP_FULL("bus_voltage", "V", String(voltage, 1), 31317, "U16", 0.1f));
    }

    // Index 17: 31318 - Unused/gap, skip

    // Index 18-19: 31319-31320 - PV1 voltage/current handled in readPVInputs

    return true;
}

bool SolplanetASW::readPVInputs(std::vector<TelemetryPoint> &out)
{
    // BULK READ OPTIMIZATION: Read 31319-31338 in one transaction (20 registers = 10 MPPT inputs)
    // Covers PV1 through PV10 voltage/current pairs
    uint16_t pvBulk[20];
    if (!modbus.readInputRegisters(slaveAddr, inputRegisterToModbus(31319), 20, pvBulk))
    {
        ESPLogger::error("Bulk read 31319-31338 (PV inputs) failed");
        return false;
    }

    // Parse MPPT inputs dynamically based on detected count
    for (uint8_t i = 0; i < mpptCount && i < 10; i++)
    {
        uint16_t pvBaseReg = 31319 + (i * 2);
        uint16_t voltage_raw = pvBulk[i * 2];
        uint16_t current_raw = pvBulk[i * 2 + 1];

        if (!isNaN_U16(voltage_raw) && !isNaN_U16(current_raw))
        {
            float pvVoltage = voltage_raw * 0.1f;
            float pvCurrent = current_raw * 0.01f;

            // Only add if PV input is actually connected (voltage > 10V)
            if (pvVoltage > 10.0f)
            {
                String pvNum = String(i + 1);
                out.push_back(TP_FULL("pv" + pvNum + "_voltage", "V", String(pvVoltage, 1), pvBaseReg, "U16", 0.1f));
                out.push_back(TP_FULL("pv" + pvNum + "_current", "A", String(pvCurrent, 2), pvBaseReg + 1, "U16", 0.01f));
                out.push_back(TP_FULL("pv" + pvNum + "_power", "W", String(pvVoltage * pvCurrent, 0), pvBaseReg, "calc", 1.0f));
            }
        }
    }

    // BULK READ OPTIMIZATION: Read string currents 31339-31358 in one transaction (20 registers)
    if (stringCount > 0)
    {
        uint16_t stringBulk[20];
        if (modbus.readInputRegisters(slaveAddr, inputRegisterToModbus(31339), 20, stringBulk))
        {
            for (uint8_t i = 0; i < stringCount && i < 20; i++)
            {
                uint16_t stringReg = 31339 + i;
                if (!isNaN_U16(stringBulk[i]))
                {
                    float current = stringBulk[i] * 0.1f;
                    if (current > 0.1f)
                    {
                        String stringNum = String(i + 1);
                        out.push_back(TP_FULL("string" + stringNum + "_current", "A", String(current, 1), stringReg, "U16", 0.1f));
                    }
                }
            }
        }
        else
        {
            ESPLogger::warn("Bulk read 31339-31358 (string currents) failed");
        }
    }

    // BULK READ OPTIMIZATION: Read 31601-31606 in one transaction (6 registers)
    // Covers: PV total power and PV-side energy (DC side, before inverter conversion)
    uint16_t pvEnergyBulk[6];
    if (modbus.readInputRegisters(slaveAddr, inputRegisterToModbus(31601), 6, pvEnergyBulk))
    {
        // Index 0-1: 31601-31602 - PV total power (U32, W, scale 1.0)
        uint32_t pvPower = ((uint32_t)pvEnergyBulk[0] << 16) | pvEnergyBulk[1];
        if (!isNaN_U32(pvPower))
        {
            out.push_back(TP_FULL("pv_total_power", "W", String(pvPower), 31601, "U32", 1.0f));
        }

        // Index 2-3: 31603-31604 - PV E-Today (U32, kWh, scale 0.1)
        // This is DC-side energy before inverter losses
        uint32_t pvEnergyToday = ((uint32_t)pvEnergyBulk[2] << 16) | pvEnergyBulk[3];
        if (!isNaN_U32(pvEnergyToday))
        {
            float energyKwh = pvEnergyToday * 0.1f;
            out.push_back(TP_FULL("pv_energy_today", "kWh", String(energyKwh, 1), 31603, "U32", 0.1f));
        }

        // Index 4-5: 31605-31606 - PV E-Total (U32, kWh, scale 0.1)
        // Total lifetime PV energy on DC side
        uint32_t pvEnergyTotal = ((uint32_t)pvEnergyBulk[4] << 16) | pvEnergyBulk[5];
        if (!isNaN_U32(pvEnergyTotal))
        {
            float energyKwh = pvEnergyTotal * 0.1f;
            out.push_back(TP_FULL("pv_energy_total", "kWh", String(energyKwh, 1), 31605, "U32", 0.1f));
        }
    }

    return true;
}

bool SolplanetASW::readGridAndPhaseMeasurements(std::vector<TelemetryPoint> &out)
{
    // BULK READ OPTIMIZATION: Read 31359-31379 in ONE transaction (21 registers)
    // Covers: 3-phase data, grid measurements, AND detailed error/warning codes
    uint16_t bulk[21];
    if (!modbus.readInputRegisters(slaveAddr, inputRegisterToModbus(31359), 21, bulk))
    {
        ESPLogger::error("Bulk read 31359-31379 (grid + phase + errors) failed");
        return false;
    }

    // === THREE-PHASE DATA (only if 3-phase inverter) ===
    if (isThreePhase)
    {
        // Index 0-5: 31359-31364 - L1/L2/L3 phase voltages and currents
        if (!isNaN_U16(bulk[0]))
        {
            float l1Voltage = bulk[0] * 0.1f;
            out.push_back(TP_FULL("l1_voltage", "V", String(l1Voltage, 1), 31359, "U16", 0.1f));
        }
        if (!isNaN_U16(bulk[1]))
        {
            float l1Current = bulk[1] * 0.1f;
            out.push_back(TP_FULL("l1_current", "A", String(l1Current, 1), 31360, "U16", 0.1f));
        }
        if (!isNaN_U16(bulk[2]))
        {
            float l2Voltage = bulk[2] * 0.1f;
            out.push_back(TP_FULL("l2_voltage", "V", String(l2Voltage, 1), 31361, "U16", 0.1f));
        }
        if (!isNaN_U16(bulk[3]))
        {
            float l2Current = bulk[3] * 0.1f;
            out.push_back(TP_FULL("l2_current", "A", String(l2Current, 1), 31362, "U16", 0.1f));
        }
        if (!isNaN_U16(bulk[4]))
        {
            float l3Voltage = bulk[4] * 0.1f;
            out.push_back(TP_FULL("l3_voltage", "V", String(l3Voltage, 1), 31363, "U16", 0.1f));
        }
        if (!isNaN_U16(bulk[5]))
        {
            float l3Current = bulk[5] * 0.1f;
            out.push_back(TP_FULL("l3_current", "A", String(l3Current, 1), 31364, "U16", 0.1f));
        }

        // Index 6-8: 31365-31367 - RS/RT/ST line voltages
        if (!isNaN_U16(bulk[6]))
        {
            float rsVoltage = bulk[6] * 0.1f;
            if (rsVoltage > 10.0f)
                out.push_back(TP_FULL("rs_line_voltage", "V", String(rsVoltage, 1), 31365, "U16", 0.1f));
        }
        if (!isNaN_U16(bulk[7]))
        {
            float rtVoltage = bulk[7] * 0.1f;
            if (rtVoltage > 10.0f)
                out.push_back(TP_FULL("rt_line_voltage", "V", String(rtVoltage, 1), 31366, "U16", 0.1f));
        }
        if (!isNaN_U16(bulk[8]))
        {
            float stVoltage = bulk[8] * 0.1f;
            if (stVoltage > 10.0f)
                out.push_back(TP_FULL("st_line_voltage", "V", String(stVoltage, 1), 31367, "U16", 0.1f));
        }
    }

    // === GRID MEASUREMENTS (all inverters) ===

    // Index 9: 31368 - Grid frequency (U16, Hz, scale 0.01)
    if (!isNaN_U16(bulk[9]))
    {
        float freq = bulk[9] * 0.01f;
        out.push_back(TP_FULL("grid_frequency", "Hz", String(freq, 2), 31368, "U16", 0.01f));
    }

    // Index 10-11: 31369-31370 - Apparent power (U32, VA, scale 1.0)
    uint32_t apparentPower = ((uint32_t)bulk[10] << 16) | bulk[11];
    if (!isNaN_U32(apparentPower))
    {
        out.push_back(TP_FULL("apparent_power", "VA", String(apparentPower), 31369, "U32", 1.0f));
    }

    // Index 12-13: 31371-31372 - Active power (S32, W, scale 1.0)
    int32_t activePower = (int32_t)(((uint32_t)bulk[12] << 16) | bulk[13]);
    if (!isNaN_S32(activePower))
    {
        out.push_back(TP_FULL("active_power", "W", String(activePower), 31371, "S32", 1.0f));
    }

    // Index 14-15: 31373-31374 - Reactive power (S32, Var, scale 1.0)
    int32_t reactivePower = (int32_t)(((uint32_t)bulk[14] << 16) | bulk[15]);
    if (!isNaN_S32(reactivePower))
    {
        out.push_back(TP_FULL("reactive_power", "Var", String(reactivePower), 31373, "S32", 1.0f));
    }

    // Index 16: 31375 - Power factor (S16, scale 0.01)
    int16_t pfSigned = (int16_t)bulk[16];
    if (!isNaN_S16(pfSigned))
    {
        float pf = pfSigned * 0.01f;
        out.push_back(TP_FULL("power_factor", "", String(pf, 2), 31375, "S16", 0.01f));
    }

    // Index 17: 31376 - Unused/gap, skip

    // Index 18: 31377 - Fault state (E16) - 0=No fault, 1=Internal fault
    if (!isNaN_U16(bulk[18]))
    {
        out.push_back(TP_FULL("fault_state", "", String(bulk[18]), 31377, "E16", 1.0f));
    }

    // Index 19: 31378 - Error message code (E16) - see MB001 section 3.4 for error codes
    if (!isNaN_U16(bulk[19]))
    {
        uint16_t errorCode = bulk[19];
        out.push_back(TP_FULL("error_code", "", String(errorCode), 31378, "E16", 1.0f));

        // Add human-readable error description
        String errorDesc = getErrorDescription(errorCode);
        if (errorDesc.length() > 0)
        {
            out.push_back(TP_FULL("error_message", "", errorDesc, 31378, "E16", 1.0f));
        }
    }

    // Index 20: 31379 - Warning message code (E16) - see MB001 section 3.4 for warning codes
    if (!isNaN_U16(bulk[20]))
    {
        uint16_t warningCode = bulk[20];
        out.push_back(TP_FULL("warning_code", "", String(warningCode), 31379, "E16", 1.0f));

        // Add human-readable warning description
        String warningDesc = getWarningDescription(warningCode);
        if (warningDesc.length() > 0)
        {
            out.push_back(TP_FULL("warning_message", "", warningDesc, 31379, "E16", 1.0f));
        }
    }

    // Grid connection status (separate read, non-contiguous at 31391)
    uint16_t gridStatus;
    if (modbus.readInputRegisters(slaveAddr, inputRegisterToModbus(31391), 1, &gridStatus))
    {
        if (!isNaN_U16(gridStatus))
        {
            out.push_back(TP_FULL("grid_connection", "", gridStatus == 1 ? "Connected" : "Disconnected", 31391, "U16", 1.0f));
        }
    }

    return true;
}

bool SolplanetASW::readGridEnergyAndLoad(std::vector<TelemetryPoint> &out)
{
    // BULK READ OPTIMIZATION: Read 31630-31633 in one transaction (4 registers)
    // Covers: E-Consumption-Today at AC side, E-Generation-Today at AC side
    uint16_t acEnergyBulk[4];
    if (modbus.readInputRegisters(slaveAddr, inputRegisterToModbus(31630), 4, acEnergyBulk))
    {
        // Index 0-1: 31630-31631 - E-Consumption-Today at AC side (U32, kWh, scale 0.1)
        uint32_t consumptionToday = ((uint32_t)acEnergyBulk[0] << 16) | acEnergyBulk[1];
        if (!isNaN_U32(consumptionToday))
        {
            float energyKwh = consumptionToday * 0.1f;
            out.push_back(TP_FULL("consumption_today", "kWh", String(energyKwh, 1), 31630, "U32", 0.1f));
        }

        // Index 2-3: 31632-31633 - E-Generation-Today at AC side (U32, kWh, scale 0.1)
        uint32_t generationToday = ((uint32_t)acEnergyBulk[2] << 16) | acEnergyBulk[3];
        if (!isNaN_U32(generationToday))
        {
            float energyKwh = generationToday * 0.1f;
            out.push_back(TP_FULL("generation_today_ac", "kWh", String(energyKwh, 1), 31632, "U32", 0.1f));
        }
    }
    else
    {
        ESPLogger::warn("Bulk read 31630-31633 (AC energy) failed");
    }

    // BULK READ OPTIMIZATION: Read 31634-31636 in one transaction (3 registers)
    // Covers: EPS load basic measurements (voltage, current, frequency)
    uint16_t epsBasicBulk[3];
    if (modbus.readInputRegisters(slaveAddr, inputRegisterToModbus(31634), 3, epsBasicBulk))
    {
        // Index 0: 31634 - EPS load voltage (U16, V, scale 0.1)
        if (!isNaN_U16(epsBasicBulk[0]))
        {
            float voltage = epsBasicBulk[0] * 0.1f;
            if (voltage > 10.0f)  // Only publish if backup is active
            {
                out.push_back(TP_FULL("eps_voltage", "V", String(voltage, 1), 31634, "U16", 0.1f));
            }
        }

        // Index 1: 31635 - EPS load current (U16, A, scale 0.1)
        if (!isNaN_U16(epsBasicBulk[1]))
        {
            float current = epsBasicBulk[1] * 0.1f;
            out.push_back(TP_FULL("eps_current", "A", String(current, 1), 31635, "U16", 0.1f));
        }

        // Index 2: 31636 - EPS load frequency (U16, Hz, scale 0.01)
        if (!isNaN_U16(epsBasicBulk[2]))
        {
            float freq = epsBasicBulk[2] * 0.01f;
            if (freq > 10.0f)  // Only publish if backup is active
            {
                out.push_back(TP_FULL("eps_frequency", "Hz", String(freq, 2), 31636, "U16", 0.01f));
            }
        }
    }
    else
    {
        ESPLogger::debug("Bulk read 31634-31636 (EPS basic) failed - no backup output");
    }

    // BULK READ OPTIMIZATION: Read 31637-31644 in one transaction (8 registers)
    // Covers: EPS load power and energy
    uint16_t epsBulk[8];
    if (modbus.readInputRegisters(slaveAddr, inputRegisterToModbus(31637), 8, epsBulk))
    {
        // Index 0-1: 31637-31638 - EPS load active power (S32, W, scale 1.0)
        int32_t epsActivePower = (int32_t)(((uint32_t)epsBulk[0] << 16) | epsBulk[1]);
        if (!isNaN_S32(epsActivePower))
        {
            out.push_back(TP_FULL("eps_active_power", "W", String(epsActivePower), 31637, "S32", 1.0f));
        }

        // Index 2-3: 31639-31640 - EPS load reactive power (S32, Var, scale 1.0)
        int32_t epsReactivePower = (int32_t)(((uint32_t)epsBulk[2] << 16) | epsBulk[3]);
        if (!isNaN_S32(epsReactivePower))
        {
            out.push_back(TP_FULL("eps_reactive_power", "Var", String(epsReactivePower), 31639, "S32", 1.0f));
        }

        // Index 4-5: 31641-31642 - E-Consumption-Today at EPS load side (U32, kWh, scale 0.1)
        uint32_t epsConsumptionToday = ((uint32_t)epsBulk[4] << 16) | epsBulk[5];
        if (!isNaN_U32(epsConsumptionToday))
        {
            float energyKwh = epsConsumptionToday * 0.1f;
            out.push_back(TP_FULL("eps_consumption_today", "kWh", String(energyKwh, 1), 31641, "U32", 0.1f));
        }

        // Index 6-7: 31643-31644 - E-Consumption-Total at EPS load side (U32, kWh, scale 0.1)
        uint32_t epsConsumptionTotal = ((uint32_t)epsBulk[6] << 16) | epsBulk[7];
        if (!isNaN_U32(epsConsumptionTotal))
        {
            float energyKwh = epsConsumptionTotal * 0.1f;
            out.push_back(TP_FULL("eps_consumption_total", "kWh", String(energyKwh, 1), 31643, "U32", 0.1f));
        }
    }
    else
    {
        ESPLogger::debug("Bulk read 31637-31644 (EPS power/energy) failed - no backup output");
    }

    // BULK READ OPTIMIZATION: Read 31645-31662 in one transaction (18 registers)
    // Covers: EPS per-phase measurements (3-phase backup systems)
    if (isThreePhase)
    {
        uint16_t epsPhaseBulk[18];
        if (modbus.readInputRegisters(slaveAddr, inputRegisterToModbus(31645), 18, epsPhaseBulk))
        {
            // Index 0-5: 31645-31650 - Phase 1/2/3 voltage and current for EPS Load
            if (!isNaN_U16(epsPhaseBulk[0]))
            {
                float voltage = epsPhaseBulk[0] * 0.1f;
                if (voltage > 10.0f)
                    out.push_back(TP_FULL("eps_l1_voltage", "V", String(voltage, 1), 31645, "U16", 0.1f));
            }
            if (!isNaN_U16(epsPhaseBulk[1]))
            {
                float current = epsPhaseBulk[1] * 0.1f;
                out.push_back(TP_FULL("eps_l1_current", "A", String(current, 1), 31646, "U16", 0.1f));
            }
            if (!isNaN_U16(epsPhaseBulk[2]))
            {
                float voltage = epsPhaseBulk[2] * 0.1f;
                if (voltage > 10.0f)
                    out.push_back(TP_FULL("eps_l2_voltage", "V", String(voltage, 1), 31647, "U16", 0.1f));
            }
            if (!isNaN_U16(epsPhaseBulk[3]))
            {
                float current = epsPhaseBulk[3] * 0.1f;
                out.push_back(TP_FULL("eps_l2_current", "A", String(current, 1), 31648, "U16", 0.1f));
            }
            if (!isNaN_U16(epsPhaseBulk[4]))
            {
                float voltage = epsPhaseBulk[4] * 0.1f;
                if (voltage > 10.0f)
                    out.push_back(TP_FULL("eps_l3_voltage", "V", String(voltage, 1), 31649, "U16", 0.1f));
            }
            if (!isNaN_U16(epsPhaseBulk[5]))
            {
                float current = epsPhaseBulk[5] * 0.1f;
                out.push_back(TP_FULL("eps_l3_current", "A", String(current, 1), 31650, "U16", 0.1f));
            }

            // Index 6-17: 31651-31662 - Phase 1/2/3 active & reactive power for EPS Load
            // Phase 1 active power
            uint32_t epsL1Power = ((uint32_t)epsPhaseBulk[6] << 16) | epsPhaseBulk[7];
            if (!isNaN_U32(epsL1Power))
            {
                out.push_back(TP_FULL("eps_l1_active_power", "W", String(epsL1Power), 31651, "U32", 1.0f));
            }
            // Phase 1 reactive power
            int32_t epsL1Reactive = (int32_t)(((uint32_t)epsPhaseBulk[8] << 16) | epsPhaseBulk[9]);
            if (!isNaN_S32(epsL1Reactive))
            {
                out.push_back(TP_FULL("eps_l1_reactive_power", "Var", String(epsL1Reactive), 31653, "S32", 1.0f));
            }

            // Phase 2 active power
            uint32_t epsL2Power = ((uint32_t)epsPhaseBulk[10] << 16) | epsPhaseBulk[11];
            if (!isNaN_U32(epsL2Power))
            {
                out.push_back(TP_FULL("eps_l2_active_power", "W", String(epsL2Power), 31655, "U32", 1.0f));
            }
            // Phase 2 reactive power
            int32_t epsL2Reactive = (int32_t)(((uint32_t)epsPhaseBulk[12] << 16) | epsPhaseBulk[13]);
            if (!isNaN_S32(epsL2Reactive))
            {
                out.push_back(TP_FULL("eps_l2_reactive_power", "Var", String(epsL2Reactive), 31657, "S32", 1.0f));
            }

            // Phase 3 active power
            uint32_t epsL3Power = ((uint32_t)epsPhaseBulk[14] << 16) | epsPhaseBulk[15];
            if (!isNaN_U32(epsL3Power))
            {
                out.push_back(TP_FULL("eps_l3_active_power", "W", String(epsL3Power), 31659, "U32", 1.0f));
            }
            // Phase 3 reactive power
            int32_t epsL3Reactive = (int32_t)(((uint32_t)epsPhaseBulk[16] << 16) | epsPhaseBulk[17]);
            if (!isNaN_S32(epsL3Reactive))
            {
                out.push_back(TP_FULL("eps_l3_reactive_power", "Var", String(epsL3Reactive), 31661, "S32", 1.0f));
            }
        }
        else
        {
            ESPLogger::debug("Bulk read 31645-31662 (EPS per-phase) failed - no 3-phase backup");
        }
    }

    // BULK READ OPTIMIZATION: Read 31663-31678 in one transaction (16 registers)
    // Covers: Grid power per phase (3-phase) and grid export energy
    uint16_t gridBulk[16];
    if (modbus.readInputRegisters(slaveAddr, inputRegisterToModbus(31663), 16, gridBulk))
    {
        if (isThreePhase)
        {
            // Index 0-1: 31663-31664 - Phase 1 active power for Grid (U32, W, scale 1.0)
            uint32_t gridL1Power = ((uint32_t)gridBulk[0] << 16) | gridBulk[1];
            if (!isNaN_U32(gridL1Power))
            {
                out.push_back(TP_FULL("grid_l1_power", "W", String(gridL1Power), 31663, "U32", 1.0f));
            }

            // Index 2-3: 31665-31666 - Phase 1 reactive power for Grid (S32, Var, scale 1.0)
            int32_t gridL1Reactive = (int32_t)(((uint32_t)gridBulk[2] << 16) | gridBulk[3]);
            if (!isNaN_S32(gridL1Reactive))
            {
                out.push_back(TP_FULL("grid_l1_reactive", "Var", String(gridL1Reactive), 31665, "S32", 1.0f));
            }

            // Index 4-5: 31667-31668 - Phase 2 active power for Grid (U32, W, scale 1.0)
            uint32_t gridL2Power = ((uint32_t)gridBulk[4] << 16) | gridBulk[5];
            if (!isNaN_U32(gridL2Power))
            {
                out.push_back(TP_FULL("grid_l2_power", "W", String(gridL2Power), 31667, "U32", 1.0f));
            }

            // Index 6-7: 31669-31670 - Phase 2 reactive power for Grid (S32, Var, scale 1.0)
            int32_t gridL2Reactive = (int32_t)(((uint32_t)gridBulk[6] << 16) | gridBulk[7]);
            if (!isNaN_S32(gridL2Reactive))
            {
                out.push_back(TP_FULL("grid_l2_reactive", "Var", String(gridL2Reactive), 31669, "S32", 1.0f));
            }

            // Index 8-9: 31671-31672 - Phase 3 active power for Grid (U32, W, scale 1.0)
            uint32_t gridL3Power = ((uint32_t)gridBulk[8] << 16) | gridBulk[9];
            if (!isNaN_U32(gridL3Power))
            {
                out.push_back(TP_FULL("grid_l3_power", "W", String(gridL3Power), 31671, "U32", 1.0f));
            }

            // Index 10-11: 31673-31674 - Phase 3 reactive power for Grid (S32, Var, scale 1.0)
            int32_t gridL3Reactive = (int32_t)(((uint32_t)gridBulk[10] << 16) | gridBulk[11]);
            if (!isNaN_S32(gridL3Reactive))
            {
                out.push_back(TP_FULL("grid_l3_reactive", "Var", String(gridL3Reactive), 31673, "S32", 1.0f));
            }
        }

        // Index 12-13: 31675-31676 - Energy charge today for Grid (U32, kWh, scale 0.1)
        uint32_t gridExportToday = ((uint32_t)gridBulk[12] << 16) | gridBulk[13];
        if (!isNaN_U32(gridExportToday))
        {
            float energyKwh = gridExportToday * 0.1f;
            out.push_back(TP_FULL("grid_export_today", "kWh", String(energyKwh, 1), 31675, "U32", 0.1f));
        }

        // Index 14-15: 31677-31678 - Energy charge total for Grid (U32, kWh, scale 0.1)
        uint32_t gridExportTotal = ((uint32_t)gridBulk[14] << 16) | gridBulk[15];
        if (!isNaN_U32(gridExportTotal))
        {
            float energyKwh = gridExportTotal * 0.1f;
            out.push_back(TP_FULL("grid_export_total", "kWh", String(energyKwh, 1), 31677, "U32", 0.1f));
        }
    }
    else
    {
        ESPLogger::warn("Bulk read 31663-31678 (grid power/energy) failed");
    }

    return true; // Non-fatal if some reads fail
}

bool SolplanetASW::readStorage(std::vector<TelemetryPoint> &out)
{
    out.clear();

    // Only read battery data if user enabled it in config
    if (!config.getBool("read_storage_regs", false))
    {
        return false;
    }

    // BULK READ OPTIMIZATION: Read 31607-31629 in one transaction (23 registers)
    // Covers: BMS1 battery communication, status, voltage, current, power, temperature, SOC, SOH, charge/discharge energy
    uint16_t batteryBulk[23];
    if (!modbus.readInputRegisters(slaveAddr, inputRegisterToModbus(31607), 23, batteryBulk))
    {
        ESPLogger::error("Bulk read 31607-31629 (battery) failed");
        return false;
    }

    // Index 0: 31607 - BMS1 Battery communication status (E16)
    // 0x000A=Normal, 0x0005=Error
    if (!isNaN_U16(batteryBulk[0]))
    {
        out.push_back(TP_FULL("battery_comm_status", "", String(batteryBulk[0], HEX), 31607, "E16", 1.0f));
    }

    // Index 1: 31608 - BMS1 Battery status (E16)
    // 0=N/A, 1=Idle, 2=Charging, 3=Discharging, 4=Error
    if (!isNaN_U16(batteryBulk[1]))
    {
        uint16_t bmsStatus = batteryBulk[1];
        out.push_back(TP_FULL("battery_status", "", String(bmsStatus), 31608, "E16", 1.0f));
        String statusText;
        switch (bmsStatus)
        {
        case 0: statusText = "N/A"; break;
        case 1: statusText = "Idle"; break;
        case 2: statusText = "Charging"; break;
        case 3: statusText = "Discharging"; break;
        case 4: statusText = "Error"; break;
        default: statusText = String(bmsStatus);
        }
        out.push_back(TP_FULL("battery_status_text", "", statusText, 31608, "E16", 1.0f));
    }

    // Index 2-9: 31609-31616 - Battery error/warning status registers (skip for now, detailed bit fields)

    // Index 10: 31617 - BMS1 Battery voltage (U16, V, scale 0.01)
    if (!isNaN_U16(batteryBulk[10]))
    {
        float voltage = batteryBulk[10] * 0.01f;
        out.push_back(TP_FULL("battery_voltage", "V", String(voltage, 2), 31617, "U16", 0.01f));
    }

    // Index 11: 31618 - BMS1 Battery current (S16, A, scale 0.1)
    // Positive = charging, Negative = discharging
    int16_t currentSigned = (int16_t)batteryBulk[11];
    if (!isNaN_S16(currentSigned))
    {
        float current = currentSigned * 0.1f;
        out.push_back(TP_FULL("battery_current", "A", String(current, 1), 31618, "S16", 0.1f));
    }

    // Index 12-13: 31619-31620 - BMS1 Battery power (S32, W, scale 1.0)
    int32_t power = (int32_t)(((uint32_t)batteryBulk[12] << 16) | batteryBulk[13]);
    if (!isNaN_S32(power))
    {
        out.push_back(TP_FULL("battery_power", "W", String(power), 31619, "S32", 1.0f));
    }

    // Index 14: 31621 - BMS1 Battery temperature (S16, °C, scale 0.1)
    int16_t tempSigned = (int16_t)batteryBulk[14];
    if (!isNaN_S16(tempSigned))
    {
        float temperature = tempSigned * 0.1f;
        out.push_back(TP_FULL("battery_temperature", "C", String(temperature, 1), 31621, "S16", 0.1f));
    }

    // Index 15: 31622 - BMS1 Battery SOC (U16, %)
    // NOTE: Modbus doc says scale 0.01, but actual register returns 0-100 directly (not 0-10000)
    if (!isNaN_U16(batteryBulk[15]))
    {
        out.push_back(TP_FULL("battery_soc", "%", String(batteryBulk[15]), 31622, "U16", 1.0f));
    }

    // Index 16: 31623 - BMS1 Battery SOH (U16, %)
    // Same issue as SOC - returns 0-100 directly
    if (!isNaN_U16(batteryBulk[16]))
    {
        out.push_back(TP_FULL("battery_soh", "%", String(batteryBulk[16]), 31623, "U16", 1.0f));
    }

    // Index 17: 31624 - BMS1 Battery charging current limit (U16, A, scale 0.1)
    if (!isNaN_U16(batteryBulk[17]))
    {
        float limit = batteryBulk[17] * 0.1f;
        out.push_back(TP_FULL("battery_charge_limit", "A", String(limit, 1), 31624, "U16", 0.1f));
    }

    // Index 18: 31625 - BMS1 Battery discharge current limit (U16, A, scale 0.1)
    if (!isNaN_U16(batteryBulk[18]))
    {
        float limit = batteryBulk[18] * 0.1f;
        out.push_back(TP_FULL("battery_discharge_limit", "A", String(limit, 1), 31625, "U16", 0.1f));
    }

    // Index 19-20: 31626-31627 - BMS1 Battery E-Charge-Today (U32, kWh, scale 0.1)
    uint32_t chargeToday = ((uint32_t)batteryBulk[19] << 16) | batteryBulk[20];
    if (!isNaN_U32(chargeToday))
    {
        float energyKwh = chargeToday * 0.1f;
        out.push_back(TP_FULL("battery_charge_today", "kWh", String(energyKwh, 1), 31626, "U32", 0.1f));
    }

    // Index 21-22: 31628-31629 - BMS1 Battery E-Discharge-Today (U32, kWh, scale 0.1)
    uint32_t dischargeToday = ((uint32_t)batteryBulk[21] << 16) | batteryBulk[22];
    if (!isNaN_U32(dischargeToday))
    {
        float energyKwh = dischargeToday * 0.1f;
        out.push_back(TP_FULL("battery_discharge_today", "kWh", String(energyKwh, 1), 31628, "U32", 0.1f));
    }

    // BULK READ: BMS1 advanced diagnostics (31679-31681, 3 registers)
    uint16_t bmsDiagBulk[3];
    if (modbus.readInputRegisters(slaveAddr, inputRegisterToModbus(31679), 3, bmsDiagBulk))
    {
        // Index 0: 31679 - BMS1 Insulation resistance (U16, kΩ, scale 1.0)
        if (!isNaN_U16(bmsDiagBulk[0]))
        {
            out.push_back(TP_FULL("battery_insulation_resistance", "kOhm", String(bmsDiagBulk[0]), 31679, "U16", 1.0f));
        }

        // Index 1: 31680 - BMS1 Charge/discharge cycles (U16, scale 1.0)
        if (!isNaN_U16(bmsDiagBulk[1]))
        {
            out.push_back(TP_FULL("battery_cycles", "", String(bmsDiagBulk[1]), 31680, "U16", 1.0f));
        }

        // Index 2: 31681 - BMS1 Environment temperature (U16, °C, scale 0.1)
        if (!isNaN_U16(bmsDiagBulk[2]))
        {
            float envTemp = bmsDiagBulk[2] * 0.1f;
            out.push_back(TP_FULL("battery_env_temperature", "C", String(envTemp, 1), 31681, "U16", 0.1f));
        }
    }
    else
    {
        ESPLogger::debug("Bulk read 31679-31681 (BMS diagnostics) failed - not critical");
    }

    // BULK READ: Diesel generator power (31685-31687, 3 registers)
    uint16_t genBulk[3];
    if (modbus.readInputRegisters(slaveAddr, inputRegisterToModbus(31685), 3, genBulk))
    {
        // Index 0-1: 31685-31686 - Oil engine power (S32, W, scale 1.0)
        int32_t genPower = (int32_t)(((uint32_t)genBulk[0] << 16) | genBulk[1]);
        if (!isNaN_S32(genPower))
        {
            out.push_back(TP_FULL("generator_power", "W", String(genPower), 31685, "S32", 1.0f));
        }

        // Index 2: 31687 - The power of the daily oil engine (U32, kWh, scale 0.1)
        // Note: Register 31687 is a single U16, not U32 as documented - treating as U16 for now
        if (!isNaN_U16(genBulk[2]))
        {
            float genEnergyKwh = genBulk[2] * 0.1f;
            out.push_back(TP_FULL("generator_energy_today", "kWh", String(genEnergyKwh, 1), 31687, "U16", 0.1f));
        }
    }
    else
    {
        ESPLogger::debug("Bulk read 31685-31687 (generator) failed - likely no gen-set connected");
    }

    ESPLogger::info("Read %d battery telemetry points", out.size());
    return !out.empty();
}

String SolplanetASW::getErrorDescription(uint16_t errorCode) const
{
    // Error codes from MB001_ASW GEN-Modbus section 3.4
    // Use PROGMEM strings to save RAM
    switch (errorCode)
    {
    case 0: return F("No Error");
    case 10: return F("Device Fault");
    case 49: return F("PV1 Lightning Arrester Fault");
    case 50: return F("PV2 Lightning Arrester Fault");
    case 52: return F("Neutral Line Loss Fault");
    case 56: return F("GFCI Protect Fault (30mA)");
    case 57: return F("GFCI Protect Fault (60mA)");
    case 58: return F("GFCI Protect Fault (150mA)");
    case 63: return F("L-PE Short-Circuit Protection");
    case 64: return F("PV Input Mode Error");
    case 66: return F("PV1 Reverse Connection Fault");
    case 67: return F("PV2 Reverse Connection Fault");
    case 68: return F("PV3 Reverse Connection Fault");
    default: return String(""); // Unknown error code - return empty string
    }
}

String SolplanetASW::getWarningDescription(uint16_t warningCode) const
{
    // Warning codes from MB001_ASW GEN-Modbus section 3.4
    // Use PROGMEM strings to save RAM
    switch (warningCode)
    {
    case 0: return F("No Warning");
    case 30: return F("Recover From Warning");
    case 150: return F("SPD Damaged");
    case 156: return F("Internal Fan Warning");
    case 157: return F("External Fan Warning");
    case 163: return F("String Current Abnormal");
    case 165: return F("Ground Connect Warning");
    case 166: return F("CPU Self-Test: Register Abnormal");
    case 167: return F("CPU Self-Test: RAM Abnormal");
    case 168: return F("CPU Self-Test: ROM Abnormal");
    case 174: return F("Low Air Temperature");
    case 175: return F("Battery SOC Low");
    case 176: return F("Battery Fault Status");
    case 177: return F("Battery Communication Disconnect");
    case 178: return F("EPS Output Over");
    default: return String(""); // Unknown warning code - return empty string
    }
}

String SolplanetASW::getGridCodeDescription(uint16_t gridCode) const
{
    // Grid codes from MB001_ASW GEN-Modbus section 3.5
    // These are country/region-specific grid connection standards
    // Use PROGMEM strings to save RAM (~2KB of strings here)
    switch (gridCode)
    {
    case 3: return F("Default");
    case 8: return F("GR PPC");
    case 35: return F("NB/T32004:2018 (China)");
    case 47: return F("AU AS 4777.2:2015 (Australia)");
    case 48: return F("NZ AS 4777.2:2015 (New Zealand)");
    case 49: return F("EN50549-1 50Hz (Europe)");
    case 50: return F("EN50549-1 60Hz (Europe)");
    case 51: return F("TOR Erzeuger Typ A V1.1 (Germany)");
    case 59: return F("CNS15382:2018 (Taiwan)");
    case 64: return F("EN 50549-1 (Europe)");
    case 65: return F("NL EN50549-1:2019 (Netherlands)");
    case 66: return F("BR NBR 16149:2013 (Brazil)");
    case 67: return F("VDE0126-1-1/A1/VFR (Germany)");
    case 68: return F("IEC 61727 50Hz");
    case 69: return F("C10/11:2019 (Belgium)");
    case 70: return F("VDE-AR-N4105:2018 (Germany)");
    case 71: return F("IEC 61727 60Hz");
    case 72: return F("G98/1 (UK)");
    case 73: return F("G99/1 (UK)");
    case 74: return F("AS/NZS4777.2:2020 Type A (Australia)");
    case 75: return F("AS/NZS4777.2:2020 Type B (Australia)");
    case 76: return F("AS/NZS4777.2:2020 Type C (Australia)");
    case 77: return F("AS/NZS4777.2:2020 (New Zealand)");
    case 78: return F("IL SI4777.3 (Israel)");
    case 79: return F("KR KS C 8565:2020 (South Korea)");
    case 80: return F("ES UNE206007-1 (Spain)");
    case 81: return F("CY EN50549-1 (Cyprus)");
    case 82: return F("CS PPDS A1 (Czech Republic)");
    case 83: return F("PL EN50549-1 (Poland)");
    case 84: return F("CEI 0-21:2019 (Italy)");
    case 85: return F("DK EN50549-1 (Denmark)");
    case 86: return F("CH NA/EEA-NE7 (Switzerland)");
    case 89: return F("RO Order208 (Romania)");
    case 90: return F("SI EN50549-1 (Slovenia)");
    case 91: return F("LV EN50549-1 (Latvia)");
    case 92: return F("VDE0126/VFR2019 IS 50Hz (Iceland)");
    case 93: return F("VDE0126/VFR2019 IS 60Hz (Iceland)");
    case 94: return F("ZA NRS 097-2-1:2017 (South Africa)");
    case 95: return F("BR PORTARIA No.140 (Brazil)");
    case 96: return F("NTS 631 Type A (Thailand)");
    case 97: return F("NTS 631 Type B (Thailand)");
    case 98: return F("NO EN50549-1 (Norway)");
    case 99: return F("VDE-AR-N 4110 (Germany)");
    case 100: return F("EN 50549-2 (Europe)");
    case 101: return F("DEWA:2016 (UAE)");
    case 102: return F("DK1 EN50549-1 (Denmark)");
    case 103: return F("ZA RPPs (South Africa)");
    default: return String(""); // Unknown grid code - return empty string
    }
}
