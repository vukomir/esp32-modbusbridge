// Temporary diagnostic code to add to your main.cpp for device discovery
// Add this to your loop() function or create a separate diagnostic mode

#include "ModbusScanner.h"

void runModbusDiagnostics()
{
    static bool diagnosticsRun = false;
    static unsigned long lastDiagnostic = 0;

    // Run diagnostics once every 30 seconds
    if (!diagnosticsRun || (millis() - lastDiagnostic > 30000))
    {
        ESPLogger::info("🔍 Running Modbus diagnostics...");

        ModbusScanner scanner(modbusClient);

        // Scan common slave addresses
        ESPLogger::info("Scanning slave addresses 1-10...");
        auto devices = scanner.scanAddresses(1, 10);
        scanner.printScanResults(devices);

        // Test your specific address (20)
        ESPLogger::info("Testing configured slave address 20...");
        auto device20 = scanner.getDeviceInfo(20);
        if (device20.responding)
        {
            ESPLogger::info("✅ Slave 20: %s", scanner.formatDeviceInfo(device20).c_str());
        }
        else
        {
            ESPLogger::warn("❌ Slave 20: No response");
        }

        // Test the working simulator address
        ESPLogger::info("Testing potential simulator addresses...");
        for (uint8_t addr = 1; addr <= 5; addr++)
        {
            auto deviceInfo = scanner.getDeviceInfo(addr);
            if (deviceInfo.responding)
            {
                ESPLogger::info("✅ Found working device at address %d: %s",
                                addr, scanner.formatDeviceInfo(deviceInfo).c_str());
            }
        }

        diagnosticsRun = true;
        lastDiagnostic = millis();
    }
}

// Add this call to your main loop() function:
// runModbusDiagnostics();
