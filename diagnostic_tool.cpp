/*
 * Comprehensive Modbus Diagnostic Tool
 * Add this code to your main.cpp to diagnose the connection issue
 *
 * Usage: Add runModbusDiagnostics() to your loop() function
 */

#include "ModbusClient.h"
#include "Config.h"
#include <ESPLogger.h>

void runModbusDiagnostics()
{
    static bool diagnosticsRun = false;
    static unsigned long lastDiagnostic = 0;

    // Run diagnostics every 30 seconds
    if (diagnosticsRun && (millis() - lastDiagnostic < 30000))
    {
        return;
    }

    ESPLogger::info("🔍 === MODBUS DIAGNOSTIC TOOL ===");

    // 1. Check current configuration
    ESPLogger::info("📋 Current Configuration:");
    ESPLogger::info("   Device Model: %s", config.getString("device_model").c_str());
    ESPLogger::info("   Slave Address: %d", config.getInt("rtu_addr"));
    ESPLogger::info("   Baud Rate: %d", config.getInt("baudrate"));
    ESPLogger::info("   Data Bits: %d", config.getInt("data_bits"));
    ESPLogger::info("   Parity: %s", config.getString("parity").c_str());
    ESPLogger::info("   Stop Bits: %d", config.getInt("stop_bits"));
    ESPLogger::info("   DE/RE Pin: %d", config.getInt("rs485_de_re_pin"));

    // 2. Test MAX485 module
    ESPLogger::info("🔌 Testing MAX485 Module:");
    bool max485Connected = modbusClient.isMAX485Connected();
    ESPLogger::info("   MAX485 Status: %s", max485Connected ? "✅ CONNECTED" : "❌ NOT CONNECTED");

    if (!max485Connected)
    {
        ESPLogger::error("❌ MAX485 module not detected - cannot proceed with device testing");
        return;
    }

    // 3. Scan for devices on common addresses
    ESPLogger::info("🔍 Scanning for Modbus devices...");
    uint8_t addressesToTest[] = {1, 2, 3, 5, 10, 20, 247};
    int addressCount = sizeof(addressesToTest) / sizeof(addressesToTest[0]);

    bool foundAnyDevice = false;

    for (int i = 0; i < addressCount; i++)
    {
        uint8_t addr = addressesToTest[i];
        ESPLogger::info("   Testing address %d...", addr);

        // Test with register 0x0000 (which your simulator supports)
        uint16_t testData;
        unsigned long startTime = millis();
        bool result = modbusClient.readHoldingRegisters(addr, 0x0000, 1, &testData);
        unsigned long responseTime = millis() - startTime;

        if (result)
        {
            ESPLogger::info("   ✅ FOUND DEVICE at address %d", addr);
            ESPLogger::info("      Register 0x0000 value: %d", testData);
            ESPLogger::info("      Response time: %lu ms", responseTime);
            foundAnyDevice = true;

            // Test a few more registers to understand the device
            ESPLogger::info("      Testing additional registers:");

            uint16_t reg0001;
            if (modbusClient.readHoldingRegisters(addr, 0x0001, 1, &reg0001))
            {
                ESPLogger::info("         Register 0x0001: %d", reg0001);
            }

            uint16_t reg000C;
            if (modbusClient.readHoldingRegisters(addr, 0x000C, 1, &reg000C))
            {
                ESPLogger::info("         Register 0x000C: %d (%.1f V if voltage)", reg000C, reg000C * 0.1f);
            }

            uint16_t reg000D;
            if (modbusClient.readHoldingRegisters(addr, 0x000D, 1, &reg000D))
            {
                ESPLogger::info("         Register 0x000D: %d (%.2f A if current)", reg000D, reg000D * 0.01f);
            }
        }
        else
        {
            ESPLogger::debug("   ❌ No response from address %d", addr);
        }

        delay(200); // Small delay between tests
    }

    // 4. Summary and recommendations
    ESPLogger::info("📊 Diagnostic Summary:");
    if (foundAnyDevice)
    {
        ESPLogger::info("✅ Found working Modbus device(s)");
        ESPLogger::info("💡 If the working address is different from your configured address (%d):",
                        config.getInt("rtu_addr"));
        ESPLogger::info("   1. Update RTU address in WebUI to match working device");
        ESPLogger::info("   2. Or change your simulator's slave address to %d", config.getInt("rtu_addr"));
    }
    else
    {
        ESPLogger::warn("❌ No Modbus devices found");
        ESPLogger::warn("🔧 Troubleshooting steps:");
        ESPLogger::warn("   1. Check RS485 A/B wiring");
        ESPLogger::warn("   2. Verify device power");
        ESPLogger::warn("   3. Check baud rate (try 9600, 19200)");
        ESPLogger::warn("   4. Verify device slave address");
        ESPLogger::warn("   5. Check RS485 termination resistors");
    }

    // 5. Test your specific simulator configuration
    ESPLogger::info("🎯 Testing Your Simulator Configuration:");
    ESPLogger::info("   Expected: Address 20, Register 0x0000 should contain energy data");

    uint16_t simData;
    if (modbusClient.readHoldingRegisters(20, 0x0000, 1, &simData))
    {
        ESPLogger::info("   ✅ Simulator responding! Register 0x0000 = %d", simData);
        ESPLogger::info("   💡 Energy value: %.2f kWh", simData * 0.01f);

        // Test reading 2 registers for 32-bit value
        uint16_t energyRegs[2];
        if (modbusClient.readHoldingRegisters(20, 0x0000, 2, energyRegs))
        {
            uint32_t totalEnergy = ((uint32_t)energyRegs[1] << 16) | energyRegs[0];
            ESPLogger::info("   ✅ 32-bit energy read successful: %.2f kWh", totalEnergy * 0.01f);
        }
    }
    else
    {
        ESPLogger::error("   ❌ Simulator at address 20 not responding");
        ESPLogger::error("   🔧 Check simulator configuration:");
        ESPLogger::error("      - Simulator slave ID should be 20");
        ESPLogger::error("      - Baud rate should be 9600");
        ESPLogger::error("      - Data format should be 8N1");
    }

    diagnosticsRun = true;
    lastDiagnostic = millis();

    ESPLogger::info("🔍 === END DIAGNOSTIC ===");
}

/*
 * HOW TO USE:
 *
 * 1. Add this to your main.cpp at the top (after includes):
 *    void runModbusDiagnostics();
 *
 * 2. Add this line in your loop() function:
 *    runModbusDiagnostics();
 *
 * 3. Upload firmware and monitor serial output
 *
 * 4. The diagnostic will run every 30 seconds and show:
 *    - Current configuration
 *    - MAX485 status
 *    - Device scan results
 *    - Specific simulator testing
 *    - Troubleshooting recommendations
 */
