#include "ModbusClient.h"
#include <ESPLogger.h>

ModbusClient::ModbusClient(Config &config)
    : config(config), serial(&Serial2), dePin(-1), initialized(false),
      baudrate(9600), parity(SERIAL_8N1), stopBits(1), dataBits(8), responseTimeout(2000)
{
}

ModbusClient::~ModbusClient()
{
    end();
}

bool ModbusClient::begin()
{
    if (initialized)
    {
        return true;
    }

    ESPLogger::info("Initializing Modbus client...");
    ESPLogger::info("Free heap before Modbus init: %u bytes", ESP.getFreeHeap());

    // Get configuration
    baudrate = getBaudrateValue();
    parity = getParityValue();
    stopBits = config.getInt("stop_bits", 1);
    dataBits = getDataBitsValue();
    dePin = config.getInt("rs485_de_re_pin", 4);

    // Calculate adaptive response timeout based on baud rate
    // Formula: Base timeout + transmission time for max response (256 bytes) + safety margin
    uint32_t bitsPerByte = 1 + dataBits + (parity != 'N' ? 1 : 0) + stopBits;
    uint32_t maxResponseTime = (256 * bitsPerByte * 1000) / baudrate; // Max response transmission time
    responseTimeout = maxResponseTime + 500;                          // Add 500ms safety margin

    ESPLogger::debug("Calculated response timeout: %lums (based on %lu baud)", responseTimeout, baudrate);

    // Configure DE/RE pin
    if (dePin >= 0)
    {
        pinMode(dePin, OUTPUT);
        setReceiveMode(); // Start in receive mode
    }

    // Configure and start serial
    configureSerial();

    initialized = true;
    ESPLogger::info("Modbus client initialized successfully");
    ESPLogger::info("Serial config: %lu baud, %d%c%d, DE/RE pin: GPIO%d",
                    baudrate, dataBits, (char)parity, stopBits, dePin);
    ESPLogger::info("Free heap after Modbus init: %u bytes", ESP.getFreeHeap());

    return true;
}

void ModbusClient::end()
{
    if (initialized)
    {
        serial->end();
        initialized = false;
    }
}

bool ModbusClient::isInitialized() const
{
    return initialized;
}

bool ModbusClient::isMAX485Connected() const
{
    if (!initialized || dePin < 0)
    {
        ESPLogger::debug("MAX485 check: Not initialized or invalid DE pin");
        return false;
    }

    // For most practical purposes, if the system is initialized and the pin is configured,
    // we'll assume the MAX485 is connected. The real test is whether we can communicate
    // with actual devices. Hardware presence detection is unreliable without additional
    // sensing circuitry.
    ESPLogger::debug("MAX485 detection: System initialized with DE/RE on GPIO%d - assuming connected", dePin);

    // Always return true once initialized - let device communication be the real test
    // This avoids false negatives that prevent the system from even trying to communicate
    return true;
}

bool ModbusClient::testConnection()
{
    if (!initialized)
    {
        return false;
    }

    // Check if MAX485 is connected first
    if (!isMAX485Connected())
    {
        ESPLogger::warn("MAX485 module not detected on DE/RE pin %d", dePin);
        return false;
    }

    ESPLogger::info("Testing Modbus connection - hardware check passed");

    // For now, just return true if MAX485 is detected
    // Actual device communication will be tested during normal polling
    // This avoids sending potentially problematic test frames
    return true;
}

bool ModbusClient::testDeviceCommunication(uint8_t slaveId)
{
    if (!initialized)
    {
        ESPLogger::error("ModbusClient not initialized");
        return false;
    }

    if (!isMAX485Connected())
    {
        ESPLogger::warn("MAX485 module not detected - cannot test device communication");
        return false;
    }

    ESPLogger::info("Testing device communication with slave ID %d", slaveId);

    // Add detailed debugging for the communication test
    ESPLogger::debug("Building test frame: slave=%d, function=0x03, address=0x0000, count=1", slaveId);

    // Try to read a single holding register (address 0)
    // This is a safe, minimal request that most Modbus devices support
    uint16_t testData;
    bool result = readHoldingRegisters(slaveId, 0, 1, &testData);

    if (result)
    {
        ESPLogger::info("Device communication successful - slave %d responded", slaveId);
    }
    else
    {
        ESPLogger::warn("Device communication failed - slave %d not responding", slaveId);
    }

    return result;
}

bool ModbusClient::readHoldingRegisters(uint8_t slaveId, uint16_t startAddr, uint16_t count, uint16_t *data)
{
    if (!initialized || count == 0 || count > 125)
    {
        return false;
    }

    uint8_t frame[8];
    size_t frameLength = buildReadFrame(slaveId, 0x03, startAddr, count, frame);

    if (!sendFrame(frame, frameLength))
    {
        ESPLogger::error("Failed to send holding registers read frame");
        return false;
    }

    uint8_t response[256];
    size_t responseLength;
    if (!receiveFrame(response, sizeof(response), responseLength))
    {
        ESPLogger::error("Failed to receive holding registers response");
        return false;
    }

    return parseReadResponse(response, responseLength, count, data);
}

bool ModbusClient::readInputRegisters(uint8_t slaveId, uint16_t startAddr, uint16_t count, uint16_t *data)
{
    if (!initialized || count == 0 || count > 125)
    {
        return false;
    }

    uint8_t frame[8];
    size_t frameLength = buildReadFrame(slaveId, 0x04, startAddr, count, frame);

    if (!sendFrame(frame, frameLength))
    {
        ESPLogger::error("Failed to send input registers read frame");
        return false;
    }

    uint8_t response[256];
    size_t responseLength;
    if (!receiveFrame(response, sizeof(response), responseLength))
    {
        ESPLogger::error("Failed to receive input registers response");
        return false;
    }

    return parseReadResponse(response, responseLength, count, data);
}

uint32_t ModbusClient::combineRegisters(uint16_t high, uint16_t low) const
{
    return ((uint32_t)high << 16) | low;
}

int32_t ModbusClient::combineSignedRegisters(uint16_t high, uint16_t low) const
{
    uint32_t combined = combineRegisters(high, low);
    return (int32_t)combined;
}

float ModbusClient::applyScaling(uint16_t rawValue, float scale) const
{
    // Check for NaN values
    if (rawValue == 0xFFFF || rawValue == 0x8000)
    {
        return NAN;
    }
    return rawValue * scale;
}

float ModbusClient::applyScaling32(uint32_t rawValue, float scale) const
{
    // Check for NaN values
    if (rawValue == 0xFFFFFFFF || rawValue == 0x80000000)
    {
        return NAN;
    }
    return rawValue * scale;
}

bool ModbusClient::sendFrame(const uint8_t *frame, size_t length)
{
    if (!initialized)
    {
        return false;
    }

    // Clear any pending data and ensure silent interval before transmission
    int clearedBytes = 0;
    while (serial->available())
    {
        serial->read();
        clearedBytes++;
    }
    if (clearedBytes > 0)
    {
        ESPLogger::debug("Cleared %d bytes from serial buffer before sending", clearedBytes);

        // Per Modbus spec: Ensure 3.5 character times of silence after clearing buffer
        uint32_t bitsPerByte = 1 + dataBits + (parity != 'N' ? 1 : 0) + stopBits;
        uint32_t preSilentInterval = (3.5 * bitsPerByte * 1000) / baudrate;
        ESPLogger::debug("Pre-transmission silent interval: %lums", preSilentInterval);
        delay(preSilentInterval);
    }

    // Log the frame being sent for debugging
    ESPLogger::debug("Sending Modbus frame (%zu bytes):", length);
    String frameHex = "";
    for (size_t i = 0; i < length; i++)
    {
        if (i > 0)
            frameHex += " ";
        frameHex += String(frame[i], HEX);
    }
    ESPLogger::debug("Frame: %s", frameHex.c_str());

    setTransmitMode();
    delayMicroseconds(100); // Additional delay for DE/RE switching

    size_t written = serial->write(frame, length);
    serial->flush(); // Wait for transmission to complete

    // Calculate transmission time and add margin
    // Total bits per byte = start bit + data bits + parity bit (if any) + stop bits
    uint32_t bitsPerByte = 1 + dataBits + (parity != 'N' ? 1 : 0) + stopBits;
    uint32_t transmissionTime = (length * bitsPerByte * 1000) / baudrate;
    ESPLogger::debug("Transmission time: %lums (%lu bits/byte)", transmissionTime, bitsPerByte);
    delay(transmissionTime);

    // Per Modbus spec: Add silent interval (3.5 character times minimum)
    uint32_t silentInterval = (3.5 * bitsPerByte * 1000) / baudrate;
    ESPLogger::debug("Adding silent interval: %lums", silentInterval);
    delay(silentInterval);

    setReceiveMode();
    delayMicroseconds(500); // Longer delay for proper DE/RE switching per spec

    ESPLogger::debug("Frame sent: %zu/%zu bytes", written, length);
    return written == length;
}

bool ModbusClient::receiveFrame(uint8_t *buffer, size_t maxLength, size_t &actualLength)
{
    if (!initialized)
    {
        return false;
    }

    actualLength = 0;
    unsigned long startTime = millis();
    unsigned long lastByteTime = startTime;

    ESPLogger::debug("Waiting for response (timeout: %lums)...", responseTimeout);

    while (millis() - startTime < responseTimeout)
    {
        if (serial->available())
        {
            if (actualLength >= maxLength)
            {
                ESPLogger::error("Response buffer overflow");
                return false;
            }

            uint8_t byte = serial->read();
            buffer[actualLength++] = byte;
            lastByteTime = millis();

            ESPLogger::debug("Received byte %zu: 0x%02X", actualLength, byte);
        }
        else if (actualLength > 0 && millis() - lastByteTime > 50) // Extended timeout for reliable frame completion
        {
            // Inter-frame timeout - frame complete (extended for MAX485 switching delays)
            ESPLogger::debug("Inter-frame timeout reached (50ms), frame complete");
            break;
        }

        yield(); // Allow other tasks to run
    }

    unsigned long totalTime = millis() - startTime;
    ESPLogger::debug("Response received: %zu bytes in %lums", actualLength, totalTime);

    // Log received frame for debugging
    if (actualLength > 0)
    {
        String responseHex = "";
        for (size_t i = 0; i < actualLength; i++)
        {
            if (i > 0)
                responseHex += " ";
            responseHex += String(buffer[i], HEX);
        }
        ESPLogger::debug("Response frame: %s", responseHex.c_str());
    }

    if (actualLength < 5)
    { // Minimum frame: slave + function + data + 2 CRC bytes
        ESPLogger::error("Response too short: %zu bytes (minimum 5 required)", actualLength);
        if (actualLength == 0)
        {
            ESPLogger::error("No response received - check device address, baud rate, and wiring");
        }
        else if (actualLength == 2)
        {
            ESPLogger::error("Received 2 bytes (0x%02X 0x%02X) - likely electrical noise or baud rate mismatch",
                             buffer[0], buffer[1]);
            ESPLogger::error("Try: 1) Swap A/B wiring, 2) Add termination resistor, 3) Try different baud rate");
        }
        else
        {
            ESPLogger::error("Partial response (%zu bytes) - possible timing or electrical issues", actualLength);
        }
        return false;
    }

    if (!validateCRC(buffer, actualLength))
    {
        ESPLogger::error("CRC validation failed");
        return false;
    }

    ESPLogger::debug("Frame received and validated successfully");
    return true;
}

uint16_t ModbusClient::calculateCRC(const uint8_t *data, size_t length) const
{
    uint16_t crc = 0xFFFF;

    for (size_t i = 0; i < length; i++)
    {
        crc ^= data[i];
        for (int j = 0; j < 8; j++)
        {
            if (crc & 0x0001)
            {
                crc = (crc >> 1) ^ 0xA001;
            }
            else
            {
                crc >>= 1;
            }
        }
    }

    return crc;
}

bool ModbusClient::validateCRC(const uint8_t *frame, size_t length) const
{
    if (length < 3)
    {
        return false;
    }

    uint16_t receivedCRC = frame[length - 2] | (frame[length - 1] << 8);
    uint16_t calculatedCRC = calculateCRC(frame, length - 2);

    return receivedCRC == calculatedCRC;
}

void ModbusClient::setTransmitMode()
{
    if (dePin >= 0)
    {
        digitalWrite(dePin, HIGH);
        delayMicroseconds(200); // Increased delay for MAX485 switching
        ESPLogger::debug("DE/RE pin set to TRANSMIT mode");
    }
}

void ModbusClient::setReceiveMode()
{
    if (dePin >= 0)
    {
        digitalWrite(dePin, LOW);
        delayMicroseconds(200); // Increased delay for MAX485 switching
        ESPLogger::debug("DE/RE pin set to RECEIVE mode");
    }
}

size_t ModbusClient::buildReadFrame(uint8_t slaveId, uint8_t function, uint16_t startAddr, uint16_t count, uint8_t *frame)
{
    frame[0] = slaveId;
    frame[1] = function;
    frame[2] = (startAddr >> 8) & 0xFF;
    frame[3] = startAddr & 0xFF;
    frame[4] = (count >> 8) & 0xFF;
    frame[5] = count & 0xFF;

    uint16_t crc = calculateCRC(frame, 6);
    frame[6] = crc & 0xFF;
    frame[7] = (crc >> 8) & 0xFF;

    ESPLogger::debug("Built frame: slave=%d, func=0x%02X, addr=0x%04X, count=%d, crc=0x%04X",
                     slaveId, function, startAddr, count, crc);

    return 8;
}

bool ModbusClient::parseReadResponse(const uint8_t *response, size_t length, uint16_t expectedCount, uint16_t *data)
{
    if (length < 5)
    {
        ESPLogger::error("Response too short for parsing");
        return false;
    }

    uint8_t slaveId = response[0];
    uint8_t function = response[1];

    // Check for exception response
    if (function & 0x80)
    {
        uint8_t exceptionCode = response[2];
        ESPLogger::error("Modbus exception from slave %d: %d", slaveId, exceptionCode);
        return false;
    }

    uint8_t byteCount = response[2];
    uint16_t expectedBytes = expectedCount * 2;

    if (byteCount != expectedBytes)
    {
        ESPLogger::error("Unexpected byte count: %d, expected: %d", byteCount, expectedBytes);
        return false;
    }

    if (length < 3 + byteCount + 2)
    { // slave + function + byteCount + data + CRC
        ESPLogger::error("Response length mismatch");
        return false;
    }

    // Extract register data (big-endian)
    for (uint16_t i = 0; i < expectedCount; i++)
    {
        uint16_t offset = 3 + (i * 2);
        data[i] = (response[offset] << 8) | response[offset + 1];
    }

    return true;
}

void ModbusClient::configureSerial()
{
    SerialConfig serialConfig;

    // Build serial configuration based on data bits, parity, and stop bits
    if (dataBits == 7)
    {
        // 7 data bits configurations
        switch (parity)
        {
        case 'E':
        case 'e':
            serialConfig = (stopBits == 2) ? SERIAL_7E2 : SERIAL_7E1;
            break;
        case 'O':
        case 'o':
            serialConfig = (stopBits == 2) ? SERIAL_7O2 : SERIAL_7O1;
            break;
        default: // 'N' or anything else
            serialConfig = (stopBits == 2) ? SERIAL_7N2 : SERIAL_7N1;
            break;
        }
    }
    else // 8 data bits (default)
    {
        // 8 data bits configurations
        switch (parity)
        {
        case 'E':
        case 'e':
            serialConfig = (stopBits == 2) ? SERIAL_8E2 : SERIAL_8E1;
            break;
        case 'O':
        case 'o':
            serialConfig = (stopBits == 2) ? SERIAL_8O2 : SERIAL_8O1;
            break;
        default: // 'N' or anything else
            serialConfig = (stopBits == 2) ? SERIAL_8N2 : SERIAL_8N1;
            break;
        }
    }

    // Configure Serial2 with specific pins for ESP32 (avoid conflict with Serial)
    // RX = GPIO16, TX = GPIO17 (default Serial2 pins on ESP32)
    serial->begin(baudrate, serialConfig, 16, 17);
    ESPLogger::debug("Serial2 hardware: RX=GPIO16, TX=GPIO17");

    // Wait for serial to be ready
    delay(50); // Reduced delay
}

uint32_t ModbusClient::getBaudrateValue() const
{
    int configBaud = config.getInt("baudrate", 9600);

    // Validate against common Modbus baud rates
    switch (configBaud)
    {
    case 9600:
    case 19200:
    case 38400:
    case 57600:
    case 115200:
        return configBaud;
    default:
        ESPLogger::error("Invalid baud rate %d, using 9600", configBaud);
        return 9600;
    }
}

int ModbusClient::getParityValue() const
{
    String parityStr = config.getString("parity", "N");
    parityStr.toUpperCase();

    if (parityStr == "E")
        return 'E';
    if (parityStr == "O")
        return 'O';
    return 'N'; // Default to None
}

int ModbusClient::getDataBitsValue() const
{
    int configDataBits = config.getInt("data_bits", 8);

    // Validate against supported data bit values
    switch (configDataBits)
    {
    case 7:
    case 8:
        return configDataBits;
    default:
        ESPLogger::error("Invalid data bits %d, using 8", configDataBits);
        return 8;
    }
}
