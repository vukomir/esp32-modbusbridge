#pragma once

#include <Arduino.h>
#include <HardwareSerial.h>
#include "Config.h"

/**
 * @brief Read-only Modbus RTU client with DE/RE control
 * SAFETY: Only implements read functions (0x03, 0x04). Write functions are NOT implemented.
 */
class ModbusClient
{
public:
    explicit ModbusClient(Config &config);
    ~ModbusClient();

    bool begin();
    void end();
    bool isInitialized() const;

    // Hardware detection
    bool isMAX485Connected() const;
    bool testConnection();
    bool testDeviceCommunication(uint8_t slaveId);

    // Read-only functions (SAFETY: No write functions implemented)
    bool readHoldingRegisters(uint8_t slaveId, uint16_t startAddr, uint16_t count, uint16_t *data);
    bool readInputRegisters(uint8_t slaveId, uint16_t startAddr, uint16_t count, uint16_t *data);

    // Utility functions for multi-register values
    uint32_t combineRegisters(uint16_t high, uint16_t low) const;
    int32_t combineSignedRegisters(uint16_t high, uint16_t low) const;
    float applyScaling(uint16_t rawValue, float scale) const;
    float applyScaling32(uint32_t rawValue, float scale) const;

private:
    Config &config;
    HardwareSerial *serial;
    int dePin;
    bool initialized;
    uint32_t baudrate;
    int parity;
    int stopBits;
    int dataBits;
    uint32_t responseTimeout;

    // Frame handling
    bool sendFrame(const uint8_t *frame, size_t length);
    bool receiveFrame(uint8_t *buffer, size_t maxLength, size_t &actualLength);
    uint16_t calculateCRC(const uint8_t *data, size_t length) const;
    bool validateCRC(const uint8_t *frame, size_t length) const;

    // DE/RE control
    void setTransmitMode();
    void setReceiveMode();

    // Frame building
    size_t buildReadFrame(uint8_t slaveId, uint8_t function, uint16_t startAddr, uint16_t count, uint8_t *frame);
    bool parseReadResponse(const uint8_t *response, size_t length, uint16_t expectedCount, uint16_t *data);

    // Configuration helpers
    void configureSerial();
    uint32_t getBaudrateValue() const;
    int getParityValue() const;
    int getDataBitsValue() const;
};
