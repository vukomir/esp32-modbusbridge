#pragma once

#include <Arduino.h>
#include <HardwareSerial.h>
#include "Config.h"
#include <vector>
#include <deque>

// Frame log entry for diagnostics
struct ModbusFrameLog
{
    uint32_t timestamp;      // millis() when frame was sent/received
    bool isTx;               // true = TX (sent), false = RX (received)
    uint8_t slaveId;
    uint8_t functionCode;
    uint16_t startAddr;
    uint16_t count;
    bool success;            // true = valid response, false = error/timeout
    uint8_t exceptionCode;   // 0 if success, Modbus exception code if error
    String hexDump;          // Hex representation of frame
    uint32_t responseTimeMs; // Response time (0 for TX frames)
};

/**
 * @brief Read-only Modbus RTU client with DE/RE control
 * SAFETY: Only implements read functions (0x03, 0x04). Write functions are NOT implemented.
 */
class ModbusClient
{
public:
    explicit ModbusClient(Config &config);
    virtual ~ModbusClient();

    bool begin();
    void end();
    bool isInitialized() const;

    // Hardware detection
    bool isMAX485Connected() const;
    bool testConnection();
    bool testDeviceCommunication(uint8_t slaveId);

    // Read-only functions (SAFETY: No write functions implemented)
    // These wrap the underlying single-shot read with retry+backoff. See MAX_READ_ATTEMPTS.
    // Set maxAttempts=1 for diagnostics to disable retries (faster, cleaner logs)
    // virtual: tests subclass with a programmable stub. Vtable cost is one slot per instance.
    virtual bool readHoldingRegisters(uint8_t slaveId, uint16_t startAddr, uint16_t count, uint16_t *data, uint8_t maxAttempts = MAX_READ_ATTEMPTS);
    virtual bool readInputRegisters(uint8_t slaveId, uint16_t startAddr, uint16_t count, uint16_t *data, uint8_t maxAttempts = MAX_READ_ATTEMPTS);

    // SAFETY: the only Modbus function codes this client is allowed to emit.
    // Any caller that synthesizes another function code is rejected by buildReadFrame().
    static constexpr uint8_t FC_READ_HOLDING_REGISTERS = 0x03;
    static constexpr uint8_t FC_READ_INPUT_REGISTERS = 0x04;

    // Retry policy for transient bus errors (CRC mismatch, timeout, partial frame).
    // Total worst-case extra wait = INITIAL_BACKOFF_MS * (2^MAX_READ_ATTEMPTS - 2)
    //                             = 100 * (8 - 2) = 600ms across the failed attempts.
    static constexpr uint8_t  MAX_READ_ATTEMPTS  = 3;
    static constexpr uint32_t INITIAL_BACKOFF_MS = 100;

    // Utility functions for multi-register values
    uint32_t combineRegisters(uint16_t high, uint16_t low) const;
    int32_t combineSignedRegisters(uint16_t high, uint16_t low) const;
    float applyScaling(uint16_t rawValue, float scale) const;
    float applyScaling32(uint32_t rawValue, float scale) const;

    // Diagnostic frame logging
    std::vector<ModbusFrameLog> getFrameLog() const; // Returns vector for API compatibility
    void clearFrameLog();

private:
    Config &config;
    HardwareSerial *serial;
    int dePin;
    int ledPin;
    bool initialized;
    uint32_t baudrate;
    int parity;
    int stopBits;
    int dataBits;
    uint32_t responseTimeout;

    // Diagnostic frame logging (circular buffer, last 100 frames)
    // Using deque for O(1) pop_front instead of vector's O(n) erase
    static constexpr size_t FRAME_LOG_SIZE = 100;
    std::deque<ModbusFrameLog> frameLog;
    void logFrame(const ModbusFrameLog &entry);
    String bytesToHex(const uint8_t *data, size_t length) const;

    // Internal: one full Modbus transaction (build + send + receive + parse). No retry.
    bool readOnce(uint8_t slaveId, uint8_t function, uint16_t startAddr, uint16_t count, uint16_t *data);

    // Internal: retry wrapper around readOnce with exponential backoff.
    bool readWithRetry(uint8_t slaveId, uint8_t function, uint16_t startAddr, uint16_t count, uint16_t *data, uint8_t maxAttempts = MAX_READ_ATTEMPTS);

    // Frame handling
    bool sendFrame(const uint8_t *frame, size_t length);
    bool receiveFrame(uint8_t *buffer, size_t maxLength, size_t &actualLength);
    uint16_t calculateCRC(const uint8_t *data, size_t length) const;
    bool validateCRC(const uint8_t *frame, size_t length) const;

    // DE/RE control
    void setTransmitMode();
    void setReceiveMode();

    // LED activity indicator
    void ledOn();
    void ledOff();
    void blinkLED(int duration = 100);

    // Frame building
    size_t buildReadFrame(uint8_t slaveId, uint8_t function, uint16_t startAddr, uint16_t count, uint8_t *frame);
    bool parseReadResponse(const uint8_t *response, size_t length, uint16_t expectedCount, uint16_t *data);

    // Configuration helpers
    void configureSerial();
    uint32_t getBaudrateValue() const;
    int getParityValue() const;
    int getDataBitsValue() const;
};
