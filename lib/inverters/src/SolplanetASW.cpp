#include "SolplanetASW.h"
#include "ModbusClient.h"
#include <ESPLogger.h>
#include <memory>

SolplanetASW::SolplanetASW(ModbusClient &modbus, Config &config)
    : modbus(modbus), config(config), slaveAddr(1), isThreePhase(false), phaseDetected(false)
{
    slaveAddr = config.getInt("rtu_addr", 1);
}

bool SolplanetASW::begin()
{
    ESPLogger::info("Initializing SolPlanet ASW inverter, slave address: %d", slaveAddr);

    if (!modbus.isInitialized())
    {
        ESPLogger::error("Modbus client not initialized");
        return false;
    }

    // Detect phase configuration on first initialization
    if (!detectPhaseConfiguration())
    {
        ESPLogger::warn("Failed to detect phase configuration, assuming single phase");
        isThreePhase = false;
    }

    ESPLogger::info("SolPlanet inverter configured as %s", isThreePhase ? "3-phase" : "1-phase");
    return true;
}

bool SolplanetASW::detectPhaseConfiguration()
{
    if (phaseDetected)
    {
        return true;
    }

    // Read device type from register 31001
    uint16_t deviceType;
    if (modbus.readInputRegisters(slaveAddr, inputRegisterToModbus(31001), 1, &deviceType))
    {
        ESPLogger::info("Device type register: 0x%04X", deviceType);

        // Device Type: 1=Single phase, 3=Three phase
        if (deviceType == 1)
        {
            isThreePhase = false;
            ESPLogger::info("Detected single-phase inverter");
        }
        else if (deviceType == 3)
        {
            isThreePhase = true;
            ESPLogger::info("Detected three-phase inverter");
        }
        else
        {
            ESPLogger::warn("Unknown device type: %d, assuming single phase", deviceType);
            isThreePhase = false;
        }

        phaseDetected = true;
        return true;
    }
    else
    {
        ESPLogger::error("Failed to read device type register");
        return false;
    }
}

bool SolplanetASW::readBasic(std::vector<TelemetryPoint> &out)
{
    out.clear();
    bool success = true;

    // Ensure phase configuration is detected
    if (!phaseDetected && !detectPhaseConfiguration())
    {
        ESPLogger::error("Cannot read without phase detection");
        return false;
    }

    // Read device information
    if (!readDeviceInfo(out))
    {
        ESPLogger::warn("Failed to read device info");
        success = false;
    }

    // Read basic measurements (energy, runtime, state)
    if (!readBasicMeasurements(out))
    {
        ESPLogger::warn("Failed to read basic measurements");
        success = false;
    }

    // Read PV inputs
    if (!readPVInputs(out))
    {
        ESPLogger::warn("Failed to read PV inputs");
        success = false;
    }

    // Read grid measurements
    if (!readGridMeasurements(out))
    {
        ESPLogger::warn("Failed to read grid measurements");
        success = false;
    }

    // Read temperatures
    if (!readTemperatures(out))
    {
        ESPLogger::warn("Failed to read temperatures");
        success = false;
    }

    // Read three-phase specific measurements if applicable
    if (isThreePhase)
    {
        if (!readThreePhaseMeasurements(out))
        {
            ESPLogger::warn("Failed to read three-phase measurements");
            success = false;
        }
    }

    ESPLogger::info("Read %d telemetry points from SolPlanet inverter", out.size());
    return success && !out.empty();
}

bool SolplanetASW::readDeviceInfo(std::vector<TelemetryPoint> &out)
{
    // Read Modbus address (31002)
    uint16_t modbusAddr;
    if (modbus.readInputRegisters(slaveAddr, inputRegisterToModbus(31002), 1, &modbusAddr))
    {
        out.push_back({"modbus_address", "", String(modbusAddr)});
    }

    // Read Serial Number (31003-31018) - 16 registers = 32 chars
    uint16_t serialRegs[16];
    if (modbus.readInputRegisters(slaveAddr, inputRegisterToModbus(31003), 16, serialRegs))
    {
        String serialNumber = "";
        for (int i = 0; i < 16; i++)
        {
            // Each register contains 2 ASCII characters
            char highChar = (serialRegs[i] >> 8) & 0xFF;
            char lowChar = serialRegs[i] & 0xFF;
            if (highChar != 0)
                serialNumber += highChar;
            if (lowChar != 0)
                serialNumber += lowChar;
        }
        serialNumber.trim();
        if (serialNumber.length() > 0)
        {
            out.push_back({"serial_number", "", serialNumber});
        }
    }

    // Read Machine Type (31019-31027) - 9 registers
    uint16_t machineTypeRegs[9];
    if (modbus.readInputRegisters(slaveAddr, inputRegisterToModbus(31019), 9, machineTypeRegs))
    {
        String machineType = "";
        for (int i = 0; i < 9; i++)
        {
            char highChar = (machineTypeRegs[i] >> 8) & 0xFF;
            char lowChar = machineTypeRegs[i] & 0xFF;
            if (highChar != 0)
                machineType += highChar;
            if (lowChar != 0)
                machineType += lowChar;
        }
        machineType.trim();
        if (machineType.length() > 0)
        {
            out.push_back({"machine_type", "", machineType});
        }
    }

    // Read Rated Power (31028-31029): U32, W, gain 1.0
    uint16_t ratedPowerRegs[2];
    if (modbus.readInputRegisters(slaveAddr, inputRegisterToModbus(31028), 2, ratedPowerRegs))
    {
        uint32_t ratedPower = modbus.combineRegisters(ratedPowerRegs[0], ratedPowerRegs[1]);
        if (ratedPower != 0xFFFFFFFF)
        {
            out.push_back({"rated_power_w", "W", String(ratedPower)});
        }
    }

    return true;
}

bool SolplanetASW::readBasicMeasurements(std::vector<TelemetryPoint> &out)
{
    // Read energy and runtime block (31301-31316)
    uint16_t energyBlock[16];
    if (modbus.readInputRegisters(slaveAddr, inputRegisterToModbus(31301), 16, energyBlock))
    {
        // Grid rated voltage (31301): U16, V, gain 0.1
        uint16_t gridRatedVoltage = energyBlock[0];
        if (gridRatedVoltage != 0xFFFF)
        {
            out.push_back({"grid_rated_voltage", "V", String(gridRatedVoltage * 0.1f, 1)});
        }

        // Grid rated frequency (31302): U16, Hz, gain 0.01
        uint16_t gridRatedFreq = energyBlock[1];
        if (gridRatedFreq != 0xFFFF)
        {
            out.push_back({"grid_rated_frequency", "Hz", String(gridRatedFreq * 0.01f, 2)});
        }

        // E-Today (31303-31304): U32, kWh, gain 0.1
        uint32_t eToday = modbus.combineRegisters(energyBlock[2], energyBlock[3]);
        if (eToday != 0xFFFFFFFF)
        {
            out.push_back({"e_today_kwh", "kWh", String(eToday * 0.1f, 1)});
        }

        // E-Total (31305-31306): U32, kWh, gain 0.1
        uint32_t eTotal = modbus.combineRegisters(energyBlock[4], energyBlock[5]);
        if (eTotal != 0xFFFFFFFF)
        {
            out.push_back({"e_total_kwh", "kWh", String(eTotal * 0.1f, 1)});
        }

        // H-Total (31307-31308): U32, hours, gain 1.0
        uint32_t hTotal = modbus.combineRegisters(energyBlock[6], energyBlock[7]);
        if (hTotal != 0xFFFFFFFF)
        {
            out.push_back({"h_total_h", "h", String(hTotal)});
        }

        // Device State (31309): E16
        uint16_t deviceState = energyBlock[8];
        if (deviceState != 0xFFFF)
        {
            String stateStr;
            switch (deviceState)
            {
            case 0:
                stateStr = "Wait";
                break;
            case 1:
                stateStr = "Normal";
                break;
            case 2:
                stateStr = "Fault";
                break;
            case 4:
                stateStr = "Checking";
                break;
            default:
                stateStr = "Unknown(" + String(deviceState) + ")";
                break;
            }
            out.push_back({"device_state", "", stateStr});
        }

        // Connect time (31310): U16, seconds, gain 1.0
        uint16_t connectTime = energyBlock[9];
        if (connectTime != 0xFFFF)
        {
            out.push_back({"connect_time_s", "s", String(connectTime)});
        }

        // Bus voltage (31317): U16, V, gain 0.1
        uint16_t busVoltage = energyBlock[16]; // Offset adjusted
        if (busVoltage != 0xFFFF)
        {
            out.push_back({"bus_voltage_v", "V", String(busVoltage * 0.1f, 1)});
        }
    }

    return true;
}

bool SolplanetASW::readPVInputs(std::vector<TelemetryPoint> &out)
{
    // Read PV inputs (31319-31338) - up to 10 PV inputs for large inverters
    uint16_t pvBlock[20];
    if (modbus.readInputRegisters(slaveAddr, inputRegisterToModbus(31319), 20, pvBlock))
    {
        // PV1-PV2 are standard, PV3-PV10 depend on inverter model
        const char *pvNames[] = {"pv1", "pv2", "pv3", "pv4", "pv5", "pv6", "pv7", "pv8", "pv9", "pv10"};

        for (int i = 0; i < 10; i++)
        {
            int voltageIdx = i * 2;
            int currentIdx = i * 2 + 1;

            if (voltageIdx >= 20 || currentIdx >= 20)
                break;

            // PV voltage: U16, V, gain 0.1
            uint16_t pvVoltage = pvBlock[voltageIdx];
            if (pvVoltage != 0xFFFF && pvVoltage > 0)
            {
                out.push_back({String(pvNames[i]) + "_voltage_v", "V", String(pvVoltage * 0.1f, 1)});

                // PV current: U16, A, gain 0.01
                uint16_t pvCurrent = pvBlock[currentIdx];
                if (pvCurrent != 0xFFFF)
                {
                    out.push_back({String(pvNames[i]) + "_current_a", "A", String(pvCurrent * 0.01f, 2)});

                    // Calculate PV power
                    float pvPower = (pvVoltage * 0.1f) * (pvCurrent * 0.01f);
                    out.push_back({String(pvNames[i]) + "_power_w", "W", String(pvPower, 1)});
                }
            }
        }
    }

    return true;
}

bool SolplanetASW::readGridMeasurements(std::vector<TelemetryPoint> &out)
{
    // Read single-phase grid measurements (31359-31379)
    uint16_t gridBlock[21];
    if (modbus.readInputRegisters(slaveAddr, inputRegisterToModbus(31359), 21, gridBlock))
    {
        // L1 Phase voltage (31359): U16, V, gain 0.1
        uint16_t l1Voltage = gridBlock[0];
        if (l1Voltage != 0xFFFF)
        {
            out.push_back({"l1_voltage_v", "V", String(l1Voltage * 0.1f, 1)});
        }

        // L1 Phase current (31360): U16, A, gain 0.1
        uint16_t l1Current = gridBlock[1];
        if (l1Current != 0xFFFF)
        {
            out.push_back({"l1_current_a", "A", String(l1Current * 0.1f, 1)});
        }

        // Grid frequency (31368): U16, Hz, gain 0.01
        uint16_t frequency = gridBlock[9];
        if (frequency != 0xFFFF)
        {
            out.push_back({"grid_frequency_hz", "Hz", String(frequency * 0.01f, 2)});
        }

        // Apparent power (31369-31370): U32, VA, gain 1.0
        uint32_t apparentPower = modbus.combineRegisters(gridBlock[10], gridBlock[11]);
        if (apparentPower != 0xFFFFFFFF)
        {
            out.push_back({"apparent_power_va", "VA", String(apparentPower)});
        }

        // Active power (31371-31372): S32, W, gain 1.0
        int32_t activePower = modbus.combineSignedRegisters(gridBlock[12], gridBlock[13]);
        if (activePower != (int32_t)0x80000000)
        {
            out.push_back({"active_power_w", "W", String(activePower)});
        }

        // Reactive power (31373-31374): S32, var, gain 1.0
        int32_t reactivePower = modbus.combineSignedRegisters(gridBlock[14], gridBlock[15]);
        if (reactivePower != (int32_t)0x80000000)
        {
            out.push_back({"reactive_power_var", "var", String(reactivePower)});
        }

        // Power factor (31375): S16, gain 0.01
        int16_t powerFactor = (int16_t)gridBlock[16];
        if (powerFactor != (int16_t)0x8000)
        {
            out.push_back({"power_factor", "", String(powerFactor * 0.01f, 2)});
        }

        // Internal fault state (31377): E16
        uint16_t faultState = gridBlock[18];
        if (faultState != 0xFFFF)
        {
            out.push_back({"internal_fault", "", String(faultState)});
        }

        // Error code (31378): E16
        uint16_t errorCode = gridBlock[19];
        if (errorCode != 0xFFFF)
        {
            out.push_back({"error_code", "", String(errorCode)});
        }

        // Warning code (31379): E16
        uint16_t warningCode = gridBlock[20];
        if (warningCode != 0xFFFF)
        {
            out.push_back({"warning_code", "", String(warningCode)});
        }
    }

    return true;
}

bool SolplanetASW::readTemperatures(std::vector<TelemetryPoint> &out)
{
    // Read temperature block (31311-31316)
    uint16_t tempBlock[6];
    if (modbus.readInputRegisters(slaveAddr, inputRegisterToModbus(31311), 6, tempBlock))
    {
        // Air temperature (31311): S16, °C, gain 0.1
        int16_t airTemp = (int16_t)tempBlock[0];
        if (airTemp != (int16_t)0x8000)
        {
            out.push_back({"air_temp_c", "°C", String(airTemp * 0.1f, 1)});
        }

        // Phase temperatures (31312-31314) for 3-phase or inverter sections
        const char *phaseNames[] = {"u_phase", "v_phase", "w_phase"};
        for (int i = 0; i < 3; i++)
        {
            int16_t phaseTemp = (int16_t)tempBlock[i + 1];
            if (phaseTemp != (int16_t)0x8000)
            {
                out.push_back({String(phaseNames[i]) + "_temp_c", "°C", String(phaseTemp * 0.1f, 1)});
            }
        }

        // Boost temperature (31315): S16, °C, gain 0.1
        int16_t boostTemp = (int16_t)tempBlock[4];
        if (boostTemp != (int16_t)0x8000)
        {
            out.push_back({"boost_temp_c", "°C", String(boostTemp * 0.1f, 1)});
        }

        // Bidirectional DC/DC Converter temperature (31316): S16, °C, gain 0.1
        int16_t dcDcTemp = (int16_t)tempBlock[5];
        if (dcDcTemp != (int16_t)0x8000)
        {
            out.push_back({"dcdc_temp_c", "°C", String(dcDcTemp * 0.1f, 1)});
        }
    }

    return true;
}

bool SolplanetASW::readThreePhaseMeasurements(std::vector<TelemetryPoint> &out)
{
    if (!isThreePhase)
    {
        return true; // Not applicable for single phase
    }

    // Read 3-phase specific measurements (31361-31367)
    uint16_t threephaseBlock[7];
    if (modbus.readInputRegisters(slaveAddr, inputRegisterToModbus(31361), 7, threephaseBlock))
    {
        // L2 Phase voltage (31361): U16, V, gain 0.1
        uint16_t l2Voltage = threephaseBlock[0];
        if (l2Voltage != 0xFFFF)
        {
            out.push_back({"l2_voltage_v", "V", String(l2Voltage * 0.1f, 1)});
        }

        // L2 Phase current (31362): U16, A, gain 0.1
        uint16_t l2Current = threephaseBlock[1];
        if (l2Current != 0xFFFF)
        {
            out.push_back({"l2_current_a", "A", String(l2Current * 0.1f, 1)});
        }

        // L3 Phase voltage (31363): U16, V, gain 0.1
        uint16_t l3Voltage = threephaseBlock[2];
        if (l3Voltage != 0xFFFF)
        {
            out.push_back({"l3_voltage_v", "V", String(l3Voltage * 0.1f, 1)});
        }

        // L3 Phase current (31364): U16, A, gain 0.1
        uint16_t l3Current = threephaseBlock[3];
        if (l3Current != 0xFFFF)
        {
            out.push_back({"l3_current_a", "A", String(l3Current * 0.1f, 1)});
        }

        // Line voltages for 3-phase (31365-31367)
        uint16_t rsVoltage = threephaseBlock[4];
        uint16_t rtVoltage = threephaseBlock[5];
        uint16_t stVoltage = threephaseBlock[6];

        if (rsVoltage != 0xFFFF)
        {
            out.push_back({"rs_line_voltage_v", "V", String(rsVoltage * 0.1f, 1)});
        }
        if (rtVoltage != 0xFFFF)
        {
            out.push_back({"rt_line_voltage_v", "V", String(rtVoltage * 0.1f, 1)});
        }
        if (stVoltage != 0xFFFF)
        {
            out.push_back({"st_line_voltage_v", "V", String(stVoltage * 0.1f, 1)});
        }
    }

    return true;
}

bool SolplanetASW::readStorage(std::vector<TelemetryPoint> &out)
{
    out.clear();

    // Only read storage registers if enabled in config
    if (!config.getBool("read_storage_regs", false))
    {
        ESPLogger::debug("Storage register reading disabled");
        return true; // Not an error
    }

    return readBatteryData(out);
}

bool SolplanetASW::readBatteryData(std::vector<TelemetryPoint> &out)
{
    ESPLogger::info("Reading storage/battery registers (31601-31681)");

    // Read battery status and communication (31607-31608)
    uint16_t batteryStatus[2];
    if (modbus.readInputRegisters(slaveAddr, inputRegisterToModbus(31607), 2, batteryStatus))
    {
        // Battery communication status (31607): E16
        uint16_t commStatus = batteryStatus[0];
        String commStatusStr = (commStatus == 0x000A) ? "Normal" : "Error";
        out.push_back({"battery_comm_status", "", commStatusStr});

        // Battery status (31608): E16
        uint16_t battStatus = batteryStatus[1];
        String battStatusStr;
        switch (battStatus)
        {
        case 0:
            battStatusStr = "Not available";
            break;
        case 1:
            battStatusStr = "Idle";
            break;
        case 2:
            battStatusStr = "Charging";
            break;
        case 3:
            battStatusStr = "Discharging";
            break;
        case 4:
            battStatusStr = "Error";
            break;
        default:
            battStatusStr = "Unknown(" + String(battStatus) + ")";
            break;
        }
        out.push_back({"battery_status", "", battStatusStr});
    }

    // Read battery measurements (31617-31625)
    uint16_t batteryMeasurements[9];
    if (modbus.readInputRegisters(slaveAddr, inputRegisterToModbus(31617), 9, batteryMeasurements))
    {
        // Battery voltage (31617): U16, V, gain 0.01
        uint16_t battVoltage = batteryMeasurements[0];
        if (battVoltage != 0xFFFF)
        {
            out.push_back({"battery_voltage_v", "V", String(battVoltage * 0.01f, 2)});
        }

        // Battery current (31618): S16, A, gain 0.1
        int16_t battCurrent = (int16_t)batteryMeasurements[1];
        if (battCurrent != (int16_t)0x8000)
        {
            out.push_back({"battery_current_a", "A", String(battCurrent * 0.1f, 1)});
        }

        // Battery power (31619-31620): S32, W, gain 1
        int32_t battPower = modbus.combineSignedRegisters(batteryMeasurements[2], batteryMeasurements[3]);
        if (battPower != (int32_t)0x80000000)
        {
            out.push_back({"battery_power_w", "W", String(battPower)});
        }

        // Battery temperature (31621): S16, °C, gain 0.1
        int16_t battTemp = (int16_t)batteryMeasurements[4];
        if (battTemp != (int16_t)0x8000)
        {
            out.push_back({"battery_temp_c", "°C", String(battTemp * 0.1f, 1)});
        }

        // Battery SOC (31622): U16, %, gain 0.01
        uint16_t battSOC = batteryMeasurements[5];
        if (battSOC != 0xFFFF)
        {
            out.push_back({"battery_soc_pct", "%", String(battSOC * 0.01f, 1)});
        }

        // Battery SOH (31623): U16, %, gain 0.01
        uint16_t battSOH = batteryMeasurements[6];
        if (battSOH != 0xFFFF)
        {
            out.push_back({"battery_soh_pct", "%", String(battSOH * 0.01f, 1)});
        }
    }

    // Read battery energy data (31626-31633)
    uint16_t batteryEnergy[8];
    if (modbus.readInputRegisters(slaveAddr, inputRegisterToModbus(31626), 8, batteryEnergy))
    {
        // Battery E-Charge-Today (31626-31627): U32, kWh, gain 0.1
        uint32_t chargeToday = modbus.combineRegisters(batteryEnergy[0], batteryEnergy[1]);
        if (chargeToday != 0xFFFFFFFF)
        {
            out.push_back({"battery_charge_today_kwh", "kWh", String(chargeToday * 0.1f, 1)});
        }

        // Battery E-Discharge-Today (31628-31629): U32, kWh, gain 0.1
        uint32_t dischargeToday = modbus.combineRegisters(batteryEnergy[2], batteryEnergy[3]);
        if (dischargeToday != 0xFFFFFFFF)
        {
            out.push_back({"battery_discharge_today_kwh", "kWh", String(dischargeToday * 0.1f, 1)});
        }

        // E-Consumption-Today at AC side (31630-31631): U32, kWh, gain 0.1
        uint32_t consumptionToday = modbus.combineRegisters(batteryEnergy[4], batteryEnergy[5]);
        if (consumptionToday != 0xFFFFFFFF)
        {
            out.push_back({"consumption_today_kwh", "kWh", String(consumptionToday * 0.1f, 1)});
        }

        // E-Generation-Today at AC side (31632-31633): U32, kWh, gain 0.1
        uint32_t generationToday = modbus.combineRegisters(batteryEnergy[6], batteryEnergy[7]);
        if (generationToday != 0xFFFFFFFF)
        {
            out.push_back({"generation_today_kwh", "kWh", String(generationToday * 0.1f, 1)});
        }
    }

    ESPLogger::info("Read %d battery telemetry points", out.size());
    return true;
}

uint16_t SolplanetASW::inputRegisterToModbus(uint16_t inputRegister) const
{
    // Convert 3xxxx input register number to 0-based Modbus address
    return inputRegister - 30001;
}
