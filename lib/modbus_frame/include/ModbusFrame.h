#pragma once

#include <stdint.h>
#include <stddef.h>
#include <string.h>

/**
 * Hardware-free Modbus RTU framing helpers.
 *
 * Deliberately header-only and free of Arduino includes so the framing rules
 * can be unit-tested on the host (test/test_modbus_frame) without a UART.
 * ModbusClient owns the serial port and timing; this owns "which bytes make a
 * frame".
 */
namespace ModbusFrame
{

inline uint16_t crc16(const uint8_t *data, size_t length)
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

// True if the last two bytes of `frame` are the CRC (low byte first) of the rest.
inline bool crcValid(const uint8_t *frame, size_t length)
{
    if (length < 3)
    {
        return false;
    }
    uint16_t received = (uint16_t)(frame[length - 2] | (frame[length - 1] << 8));
    return received == crc16(frame, length - 2);
}

// Length in bytes of a normal FC03/FC04 reply for `count` registers:
// slave + function + byte count + 2*count data + 2 CRC.
constexpr size_t readResponseLength(uint16_t count)
{
    return 5u + 2u * (size_t)count;
}

// An exception reply is always slave + (function|0x80) + code + 2 CRC.
constexpr size_t EXCEPTION_LENGTH = 5;

/**
 * Rebuilds a normal read reply whose first one or two bytes never arrived.
 *
 * Seen in the field: the reply comes in complete except for the slave address
 * (occasionally the function code as well), because the RS485 transceiver was
 * still switching back to receive when the device began to answer. Those bytes
 * are known - they are what we asked for - so they can be put back, and the
 * CRC then says whether the result really is the frame the device sent. A
 * wrong guess fails the CRC exactly as line noise would.
 *
 * `raw` holds everything received, unaligned. On success the complete frame is
 * written to `out` (which must hold expectedLength bytes) and its length is
 * returned; otherwise 0. Exception replies are deliberately not reconstructed:
 * three or four bytes are too little evidence to conclude the device refused.
 */
inline size_t recoverMissingHeader(const uint8_t *raw, size_t rawLength, uint8_t slaveId, uint8_t function,
                                   size_t expectedLength, uint8_t *out)
{
    const uint8_t header[2] = {slaveId, function};
    for (size_t missing = 1; missing <= 2; missing++)
    {
        if (expectedLength <= missing + 2 || rawLength + missing != expectedLength)
        {
            continue;
        }
        memcpy(out, header, missing);
        memcpy(out + missing, raw, rawLength);
        if (crcValid(out, expectedLength))
        {
            return expectedLength;
        }
    }
    return 0;
}

/**
 * Collects the reply to one read request, byte by byte.
 *
 * The reply to a read is fully determined by the request: it is either
 * `expectedLength` bytes or a 5-byte exception. So the frame is complete the
 * moment that many bytes are in - there is no need to wait for a stretch of
 * silence to find its end.
 *
 * Bytes ahead of the frame header are discarded. A stray byte in front of the
 * reply (line noise while the RS485 transceiver turns around is the usual
 * source) would otherwise shift everything after it by one and turn a good
 * frame into a CRC failure.
 */
class Assembler
{
public:
    Assembler(uint8_t *buffer, size_t capacity, uint8_t slaveId, uint8_t function, size_t expectedLength)
        : buf(buffer), cap(capacity), slave(slaveId), fc(function),
          expected(expectedLength > capacity ? capacity : expectedLength),
          len(0), dropped(0), done(false)
    {
    }

    // Feed one received byte. Returns true once the frame is complete; further
    // bytes are then ignored.
    bool push(uint8_t byte)
    {
        if (done || len >= cap)
        {
            return done;
        }
        buf[len++] = byte;

        // Re-align on the header: byte 0 must be our slave, byte 1 the function
        // we asked for (or its exception form).
        while (len > 0 && !headerOk())
        {
            memmove(buf, buf + 1, len - 1);
            len--;
            dropped++;
        }

        if (len >= 2 && len >= target())
        {
            done = true;
        }
        return done;
    }

    bool complete() const { return done; }
    bool isException() const { return len >= 2 && (buf[1] & 0x80); }
    size_t length() const { return len; }
    size_t discarded() const { return dropped; }

    // Bytes still expected. Never 0 while incomplete, so it is safe to use for
    // "how long may the line stay quiet" calculations.
    size_t remaining() const
    {
        if (done)
        {
            return 0;
        }
        size_t t = (len >= 2) ? target() : expected;
        return t > len ? t - len : 1;
    }

private:
    uint8_t *buf;
    size_t cap;
    uint8_t slave;
    uint8_t fc;
    size_t expected;
    size_t len;
    size_t dropped;
    bool done;

    bool headerOk() const
    {
        if (buf[0] != slave)
        {
            return false;
        }
        return len < 2 || buf[1] == fc || buf[1] == (uint8_t)(fc | 0x80);
    }

    size_t target() const { return (buf[1] & 0x80) ? EXCEPTION_LENGTH : expected; }
};

} // namespace ModbusFrame
