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

    // PV total power (separate read, non-contiguous with above)
    uint16_t pvPowerRegs[2];
    if (modbus.readInputRegisters(slaveAddr, inputRegisterToModbus(31601), 2, pvPowerRegs))
    {
        uint32_t pvPower = ((uint32_t)pvPowerRegs[0] << 16) | pvPowerRegs[1];
        if (!isNaN_U32(pvPower))
        {
            out.push_back(TP_FULL("pv_total_power", "W", String(pvPower), 31601, "U32", 1.0f));
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

bool SolplanetASW::readStorage(std::vector<TelemetryPoint> &out)
{
    out.clear();

    // Only read battery data if user enabled it in config
    if (!config.getBool("read_storage_regs", false))
    {
        return false;
    }

    // Register 31607: BMS1 Battery communication status (E16)
    // 0x000A=Normal, 0x0005=Error
    uint16_t bmsComm;
    if (modbus.readInputRegisters(slaveAddr, inputRegisterToModbus(31607), 1, &bmsComm))
    {
        if (!isNaN_U16(bmsComm))
        {
            out.push_back(TP_FULL("battery_comm_status", "", String(bmsComm, HEX), 31607, "E16", 1.0f));
        }
    }

    // Register 31608: BMS1 Battery status (E16)
    // 0=N/A, 1=Idle, 2=Charging, 3=Discharging, 4=Error
    uint16_t bmsStatus;
    if (modbus.readInputRegisters(slaveAddr, inputRegisterToModbus(31608), 1, &bmsStatus))
    {
        if (!isNaN_U16(bmsStatus))
        {
            out.push_back(TP_FULL("battery_status", "", String(bmsStatus), 31608, "E16", 1.0f));
            String statusText;
            switch (bmsStatus)
            {
            case 0:
                statusText = "N/A";
                break;
            case 1:
                statusText = "Idle";
                break;
            case 2:
                statusText = "Charging";
                break;
            case 3:
                statusText = "Discharging";
                break;
            case 4:
                statusText = "Error";
                break;
            default:
                statusText = String(bmsStatus);
            }
            out.push_back(TP_FULL("battery_status_text", "", statusText, 31608, "E16", 1.0f));
        }
    }

    // Register 31617: BMS1 Battery voltage (U16, V, scale 0.01)
    uint16_t battVoltage;
    if (modbus.readInputRegisters(slaveAddr, inputRegisterToModbus(31617), 1, &battVoltage))
    {
        if (!isNaN_U16(battVoltage))
        {
            float voltage = battVoltage * 0.01f;
            out.push_back(TP_FULL("battery_voltage", "V", String(voltage, 2), 31617, "U16", 0.01f));
        }
    }

    // Register 31618: BMS1 Battery current (S16, A, scale 0.1)
    // Positive = charging, Negative = discharging
    uint16_t battCurrent;
    if (modbus.readInputRegisters(slaveAddr, inputRegisterToModbus(31618), 1, &battCurrent))
    {
        int16_t currentSigned = (int16_t)battCurrent;
        if (!isNaN_S16(currentSigned))
        {
            float current = currentSigned * 0.1f;
            out.push_back(TP_FULL("battery_current", "A", String(current, 1), 31618, "S16", 0.1f));
        }
    }

    // Registers 31619-31620: BMS1 Battery power (S32, W, scale 1.0)
    uint16_t battPowerRegs[2];
    if (modbus.readInputRegisters(slaveAddr, inputRegisterToModbus(31619), 2, battPowerRegs))
    {
        int32_t power = (int32_t)(((uint32_t)battPowerRegs[0] << 16) | battPowerRegs[1]);
        if (!isNaN_S32(power))
        {
            out.push_back(TP_FULL("battery_power", "W", String(power), 31619, "S32", 1.0f));
        }
    }

    // Register 31621: BMS1 Battery temperature (S16, °C, scale 0.1)
    uint16_t battTemp;
    if (modbus.readInputRegisters(slaveAddr, inputRegisterToModbus(31621), 1, &battTemp))
    {
        int16_t tempSigned = (int16_t)battTemp;
        if (!isNaN_S16(tempSigned))
        {
            float temperature = tempSigned * 0.1f;
            out.push_back(TP_FULL("battery_temperature", "C", String(temperature, 1), 31621, "S16", 0.1f));
        }
    }

    // Register 31622: BMS1 Battery SOC (U16, %)
    // NOTE: Modbus doc says scale 0.01, but actual register returns 0-100 directly (not 0-10000)
    // Verified: battery at 99% returns raw value 100, not 9900
    uint16_t battSOC;
    if (modbus.readInputRegisters(slaveAddr, inputRegisterToModbus(31622), 1, &battSOC))
    {
        if (!isNaN_U16(battSOC))
        {
            // Register returns percentage directly (1-100), no scaling needed
            out.push_back(TP_FULL("battery_soc", "%", String(battSOC), 31622, "U16", 1.0f));
        }
    }

    // Register 31623: BMS1 Battery SOH (U16, %)
    // Same issue as SOC - returns 0-100 directly
    uint16_t battSOH;
    if (modbus.readInputRegisters(slaveAddr, inputRegisterToModbus(31623), 1, &battSOH))
    {
        if (!isNaN_U16(battSOH))
        {
            // Register returns percentage directly (1-100), no scaling needed
            out.push_back(TP_FULL("battery_soh", "%", String(battSOH), 31623, "U16", 1.0f));
        }
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
