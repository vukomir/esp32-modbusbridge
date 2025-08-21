#pragma once

#include <Arduino.h>
#include "ModbusClient.h"
#include <vector>

/**
 * @brief Modbus device scanner for troubleshooting and discovery
 * Helps identify devices on the RS485 bus with different addresses and configurations
 */
class ModbusScanner
{
public:
    struct DeviceInfo
    {
        uint8_t slaveId;
        bool responding;
        uint16_t testRegisterValue;
        String serialConfig;
        unsigned long responseTime;
    };

    explicit ModbusScanner(ModbusClient &modbus);

    // Scan for devices
    std::vector<DeviceInfo> scanAddresses(uint8_t startAddr = 1, uint8_t endAddr = 10);
    std::vector<DeviceInfo> scanBaudrates(uint8_t slaveId, const std::vector<uint32_t> &baudrates);

    // Test specific device
    bool testDevice(uint8_t slaveId, uint16_t testRegister = 0);
    DeviceInfo getDeviceInfo(uint8_t slaveId, uint16_t testRegister = 0);

    // Configuration helpers
    void printScanResults(const std::vector<DeviceInfo> &results);
    String formatDeviceInfo(const DeviceInfo &device);

private:
    ModbusClient &modbus;

    // Test different register addresses commonly used for device identification
    std::vector<uint16_t> getCommonTestRegisters();
};

// Common Modbus baud rates for scanning
extern const std::vector<uint32_t> COMMON_BAUDRATES;
