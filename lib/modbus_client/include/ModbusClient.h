#pragma once

#include <Arduino.h>
#include <HardwareSerial.h>
#include "Config.h"
#include "ModbusFrame.h"
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

// Why the most recent read failed. Drivers use this to tell "the device said
// no" (Exception - retrying is pointless, the block is probably unsupported)
// apart from "the bus let us down" (Timeout / BadFrame - worth another go).
enum class ModbusReadError : uint8_t
{
    None,           // last read succeeded
    Timeout,        // not a single usable byte came back
    BadFrame,       // something came back but it was short or failed CRC
    Exception,      // well-formed Modbus exception reply; see getLastExceptionCode()
    InvalidRequest  // rejected before anything was sent (count out of range, not initialized)
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
    //  - A Modbus exception reply is NOT retried: the device answered, and it
    //    will give the same answer again.
    //  - After a timeout the bus is already silent, so the retry goes out at once.
    //  - After a corrupt frame we wait RETRY_DELAY_MS (doubling per attempt) so
    //    whatever is still on the wire can finish before we transmit again.
    // Worst-case extra wait across the failed attempts = 20 + 40 = 60ms. This
    // used to be 100 + 200ms, which on its own ate a third of a 1s poll cycle.
    static constexpr uint8_t  MAX_READ_ATTEMPTS  = 3;
    static constexpr uint32_t RETRY_DELAY_MS     = 20;

    // How long we give the device to START answering, on top of the time the
    // reply itself needs on the wire at the configured baud rate.
    static constexpr uint32_t RESPONSE_TURNAROUND_MS = 500;

    // Minimum silence kept between the end of one frame and the start of the
    // next request (floor; the actual gap is 3.5 character times if longer).
    static constexpr uint32_t MIN_INTER_FRAME_GAP_MS = 5;
    uint32_t interFrameGapMs() const;

    // Outcome of the most recent read (see ModbusReadError).
    // virtual so test stubs can script failures without touching the UART.
    virtual ModbusReadError getLastError() const { return lastError; }
    virtual uint8_t getLastExceptionCode() const { return lastExceptionCode; }

    // How many replies arrived without their first byte(s) and were rebuilt
    // (see ModbusFrame::recoverMissingHeader). A steadily climbing number
    // means the TX->RX turnaround is still losing bytes: look at the
    // transceiver wiring / DE timing rather than at the bus.
    uint32_t getHeaderRecoveryCount() const { return headerRecoveries; }

    // Length in bytes of a normal FC03/FC04 reply for `count` registers:
    // slave + function + byte count + 2*count data + 2 CRC.
    static constexpr size_t responseLength(uint16_t count) { return ModbusFrame::readResponseLength(count); }

    // Milliseconds `bytes` occupy on the wire at the configured serial settings
    // (rounded up). Used for timeouts and for estimating poll cycle time.
    uint32_t frameTimeMs(size_t bytes) const;

    // Utility functions for multi-register values
    uint32_t combineRegisters(uint16_t high, uint16_t low) const;
    int32_t combineSignedRegisters(uint16_t high, uint16_t low) const;
    float applyScaling(uint16_t rawValue, float scale) const;
    float applyScaling32(uint32_t rawValue, float scale) const;

    // Diagnostic frame logging
    std::vector<ModbusFrameLog> getFrameLog() const; // Returns vector for API compatibility
    void clearFrameLog();

protected:
    // For stubs that override the read methods.
    void setLastError(ModbusReadError error, uint8_t exceptionCode = 0)
    {
        lastError = error;
        lastExceptionCode = exceptionCode;
    }

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
    ModbusReadError lastError;
    uint8_t lastExceptionCode;
    uint32_t headerRecoveries;
    unsigned long lastBusActivityMs; // last byte sent or received

    // Diagnostic frame logging (circular buffer, last 100 frames)
    // Using deque for O(1) pop_front instead of vector's O(n) erase
    static constexpr size_t FRAME_LOG_SIZE = 100;
    std::deque<ModbusFrameLog> frameLog;
    void logFrame(const ModbusFrameLog &entry);
    String bytesToHex(const uint8_t *data, size_t length) const;

    // Internal: one full Modbus transaction (build + send + receive + parse). No retry.
    // Returns ModbusReadError::None on success; exceptionOut is set for Exception.
    ModbusReadError readOnce(uint8_t slaveId, uint8_t function, uint16_t startAddr, uint16_t count,
                             uint16_t *data, uint8_t &exceptionOut);

    // Internal: retry wrapper around readOnce (policy described at MAX_READ_ATTEMPTS).
    bool readWithRetry(uint8_t slaveId, uint8_t function, uint16_t startAddr, uint16_t count, uint16_t *data, uint8_t maxAttempts = MAX_READ_ATTEMPTS);

    // Frame handling
    bool sendFrame(const uint8_t *frame, size_t length);
    // Collects the reply to a request for `expectedLength` bytes from `slaveId`.
    // Returns None (CRC-valid frame in buffer), Timeout or BadFrame.
    ModbusReadError receiveFrame(uint8_t *buffer, size_t maxLength, size_t &actualLength,
                                 uint8_t slaveId, uint8_t function, size_t expectedLength);
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
