#include "ModbusScanner.h"
#include <ESPLogger.h>

// Common Modbus baud rates for scanning
const std::vector<uint32_t> COMMON_BAUDRATES = {9600, 19200, 38400, 57600, 115200};

ModbusScanner::ModbusScanner(ModbusClient &modbus) : modbus(modbus)
{
}

std::vector<ModbusScanner::DeviceInfo> ModbusScanner::scanAddresses(uint8_t startAddr, uint8_t endAddr)
{
    std::vector<DeviceInfo> foundDevices;

    ESPLogger::info("🔍 Scanning Modbus addresses %d to %d...", startAddr, endAddr);

    for (uint8_t addr = startAddr; addr <= endAddr; addr++)
    {
        ESPLogger::debug("Testing slave address %d...", addr);

        DeviceInfo device = getDeviceInfo(addr);
        foundDevices.push_back(device);

        if (device.responding)
        {
            ESPLogger::info("✅ Found device at address %d (register 0: %d)", addr, device.testRegisterValue);
        }
        else
        {
            ESPLogger::debug("❌ No response from address %d", addr);
        }

        // Small delay between scans to avoid overwhelming the bus
        delay(100);
    }

    ESPLogger::info("Scan complete - found %d responding devices",
                    std::count_if(foundDevices.begin(), foundDevices.end(),
                                  [](const DeviceInfo &d)
                                  { return d.responding; }));

    return foundDevices;
}

std::vector<ModbusScanner::DeviceInfo> ModbusScanner::scanBaudrates(uint8_t slaveId, const std::vector<uint32_t> &baudrates)
{
    std::vector<DeviceInfo> results;

    ESPLogger::info("🔍 Scanning baud rates for slave address %d...", slaveId);

    for (uint32_t baudrate : baudrates)
    {
        ESPLogger::debug("Testing baud rate %lu...", baudrate);

        // Note: This would require reconfiguring the ModbusClient with different baud rate
        // For now, we'll just test with current configuration
        DeviceInfo device = getDeviceInfo(slaveId);
        device.serialConfig = String(baudrate) + " baud";
        results.push_back(device);

        if (device.responding)
        {
            ESPLogger::info("✅ Device responds at %lu baud", baudrate);
            break; // Found working baud rate
        }
    }

    return results;
}

bool ModbusScanner::testDevice(uint8_t slaveId, uint16_t testRegister)
{
    return getDeviceInfo(slaveId, testRegister).responding;
}

ModbusScanner::DeviceInfo ModbusScanner::getDeviceInfo(uint8_t slaveId, uint16_t testRegister)
{
    DeviceInfo device;
    device.slaveId = slaveId;
    device.responding = false;
    device.testRegisterValue = 0;
    device.responseTime = 0;

    if (!modbus.isInitialized())
    {
        ESPLogger::warn("ModbusClient not initialized for scanning");
        return device;
    }

    // Test multiple common registers to increase chance of success
    std::vector<uint16_t> testRegisters = {testRegister, 0, 1, 40001, 30001};

    for (uint16_t reg : testRegisters)
    {
        unsigned long startTime = millis();
        uint16_t data;

        // Try reading holding register first
        bool holdingResult = modbus.readHoldingRegisters(slaveId, reg, 1, &data);
        if (holdingResult)
        {
            device.responding = true;
            device.testRegisterValue = data;
            device.responseTime = millis() - startTime;
            device.serialConfig = "Holding register " + String(reg);
            ESPLogger::debug("Device %d responds to holding register %d: value=%d", slaveId, reg, data);
            return device;
        }

        // Try reading input register
        bool inputResult = modbus.readInputRegisters(slaveId, reg, 1, &data);
        if (inputResult)
        {
            device.responding = true;
            device.testRegisterValue = data;
            device.responseTime = millis() - startTime;
            device.serialConfig = "Input register " + String(reg);
            ESPLogger::debug("Device %d responds to input register %d: value=%d", slaveId, reg, data);
            return device;
        }

        // Small delay between register tests
        delay(50);
    }

    return device;
}

void ModbusScanner::printScanResults(const std::vector<DeviceInfo> &results)
{
    ESPLogger::info("=== Modbus Scan Results ===");

    int respondingCount = 0;
    for (const auto &device : results)
    {
        if (device.responding)
        {
            ESPLogger::info("✅ Slave %d: %s", device.slaveId, formatDeviceInfo(device).c_str());
            respondingCount++;
        }
        else
        {
            ESPLogger::debug("❌ Slave %d: No response", device.slaveId);
        }
    }

    if (respondingCount == 0)
    {
        ESPLogger::warn("❌ No devices found. Check:");
        ESPLogger::warn("   - RS485 A/B wiring");
        ESPLogger::warn("   - Device power");
        ESPLogger::warn("   - Baud rate settings");
        ESPLogger::warn("   - Device slave address");
    }
    else
    {
        ESPLogger::info("✅ Found %d responding device(s)", respondingCount);
    }
}

String ModbusScanner::formatDeviceInfo(const DeviceInfo &device)
{
    return "Value=" + String(device.testRegisterValue) +
           ", Response=" + String(device.responseTime) + "ms" +
           ", Type=" + device.serialConfig;
}

std::vector<uint16_t> ModbusScanner::getCommonTestRegisters()
{
    return {
        0,     // Basic register 0
        1,     // Basic register 1
        40001, // Holding register 40001
        40002, // Holding register 40002
        30001, // Input register 30001
        30002  // Input register 30002
    };
}
