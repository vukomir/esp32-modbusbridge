#include <unity.h>
#include <Arduino.h>
#include "ModbusFrame.h"
#include <vector>

// Host-side suite for the Modbus RTU framing rules. ModbusFrame.h has no
// hardware dependencies, so unlike test_modbus_client this runs for real under
// `pio test -e native` rather than only compiling.

void setUp(void) {}
void tearDown(void) {}

// Builds a valid FC03/FC04 reply: slave, fc, byte count, data..., CRC lo, CRC hi.
static std::vector<uint8_t> makeReply(uint8_t slave, uint8_t fc, const std::vector<uint16_t> &regs)
{
    std::vector<uint8_t> f;
    f.push_back(slave);
    f.push_back(fc);
    f.push_back((uint8_t)(regs.size() * 2));
    for (uint16_t r : regs)
    {
        f.push_back((uint8_t)(r >> 8));
        f.push_back((uint8_t)(r & 0xFF));
    }
    uint16_t crc = ModbusFrame::crc16(f.data(), f.size());
    f.push_back((uint8_t)(crc & 0xFF));
    f.push_back((uint8_t)(crc >> 8));
    return f;
}

static std::vector<uint8_t> makeException(uint8_t slave, uint8_t fc, uint8_t code)
{
    std::vector<uint8_t> f = {slave, (uint8_t)(fc | 0x80), code};
    uint16_t crc = ModbusFrame::crc16(f.data(), f.size());
    f.push_back((uint8_t)(crc & 0xFF));
    f.push_back((uint8_t)(crc >> 8));
    return f;
}

void test_crc_known_vector()
{
    // Read holding register 0, count 1, from slave 1: the textbook request,
    // which goes out on the wire as 01 03 00 00 00 01 84 0A.
    const uint8_t request[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x01};
    TEST_ASSERT_EQUAL_HEX16(0x0A84, ModbusFrame::crc16(request, sizeof(request)));

    const uint8_t framed[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x01, 0x84, 0x0A};
    TEST_ASSERT_TRUE(ModbusFrame::crcValid(framed, sizeof(framed)));
}

void test_crc_rejects_corruption_and_short_input()
{
    uint8_t framed[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x01, 0x84, 0x0A};
    framed[3] ^= 0x01;
    TEST_ASSERT_FALSE(ModbusFrame::crcValid(framed, sizeof(framed)));
    TEST_ASSERT_FALSE(ModbusFrame::crcValid(framed, 2));
}

void test_response_length()
{
    TEST_ASSERT_EQUAL_UINT(7, ModbusFrame::readResponseLength(1));
    TEST_ASSERT_EQUAL_UINT(45, ModbusFrame::readResponseLength(20));
    TEST_ASSERT_EQUAL_UINT(255, ModbusFrame::readResponseLength(125));
}

// The frame is complete on its last byte - no trailing silence needed.
void test_completes_exactly_at_expected_length()
{
    std::vector<uint8_t> reply = makeReply(3, 0x04, {0x1234, 0x5678, 0x9ABC});
    uint8_t buf[256];
    ModbusFrame::Assembler frame(buf, sizeof(buf), 3, 0x04, ModbusFrame::readResponseLength(3));

    for (size_t i = 0; i + 1 < reply.size(); i++)
    {
        TEST_ASSERT_FALSE(frame.push(reply[i]));
    }
    TEST_ASSERT_TRUE(frame.push(reply.back()));
    TEST_ASSERT_TRUE(frame.complete());
    TEST_ASSERT_EQUAL_UINT(reply.size(), frame.length());
    TEST_ASSERT_EQUAL_UINT(0, frame.discarded());
    TEST_ASSERT_TRUE(ModbusFrame::crcValid(buf, frame.length()));
}

// Noise ahead of the reply must not shift the frame and break its CRC.
void test_discards_stray_bytes_before_header()
{
    std::vector<uint8_t> reply = makeReply(3, 0x04, {0x0102, 0x0304});
    uint8_t buf[256];
    ModbusFrame::Assembler frame(buf, sizeof(buf), 3, 0x04, ModbusFrame::readResponseLength(2));

    frame.push(0x00);
    frame.push(0xFF);
    for (uint8_t b : reply)
    {
        frame.push(b);
    }

    TEST_ASSERT_TRUE(frame.complete());
    TEST_ASSERT_EQUAL_UINT(2, frame.discarded());
    TEST_ASSERT_EQUAL_UINT(reply.size(), frame.length());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(reply.data(), buf, reply.size());
    TEST_ASSERT_TRUE(ModbusFrame::crcValid(buf, frame.length()));
}

// A stray byte that happens to equal the slave address is the awkward case:
// it looks like a header until the next byte disproves it.
void test_discards_stray_byte_equal_to_slave_id()
{
    std::vector<uint8_t> reply = makeReply(3, 0x04, {0xBEEF});
    uint8_t buf[256];
    ModbusFrame::Assembler frame(buf, sizeof(buf), 3, 0x04, ModbusFrame::readResponseLength(1));

    frame.push(0x03); // noise, coincidentally the slave id
    for (uint8_t b : reply)
    {
        frame.push(b);
    }

    TEST_ASSERT_TRUE(frame.complete());
    TEST_ASSERT_EQUAL_UINT(1, frame.discarded());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(reply.data(), buf, reply.size());
}

// An exception is 5 bytes whatever length was asked for; do not wait for more.
void test_exception_reply_completes_at_five_bytes()
{
    std::vector<uint8_t> reply = makeException(3, 0x04, 0x02);
    uint8_t buf[256];
    ModbusFrame::Assembler frame(buf, sizeof(buf), 3, 0x04, ModbusFrame::readResponseLength(61));

    bool done = false;
    for (uint8_t b : reply)
    {
        done = frame.push(b);
    }

    TEST_ASSERT_TRUE(done);
    TEST_ASSERT_TRUE(frame.isException());
    TEST_ASSERT_EQUAL_UINT(5, frame.length());
    TEST_ASSERT_EQUAL_HEX8(0x02, buf[2]);
    TEST_ASSERT_TRUE(ModbusFrame::crcValid(buf, frame.length()));
}

void test_truncated_reply_stays_incomplete()
{
    std::vector<uint8_t> reply = makeReply(3, 0x04, {1, 2, 3, 4});
    uint8_t buf[256];
    ModbusFrame::Assembler frame(buf, sizeof(buf), 3, 0x04, ModbusFrame::readResponseLength(4));

    for (size_t i = 0; i < 6; i++)
    {
        frame.push(reply[i]);
    }

    TEST_ASSERT_FALSE(frame.complete());
    TEST_ASSERT_EQUAL_UINT(6, frame.length());
    TEST_ASSERT_EQUAL_UINT(reply.size() - 6, frame.remaining());
}

// A reply from some other device on the bus is never mistaken for ours.
void test_reply_from_other_slave_is_ignored()
{
    std::vector<uint8_t> reply = makeReply(7, 0x04, {0x1111, 0x2222});
    uint8_t buf[256];
    ModbusFrame::Assembler frame(buf, sizeof(buf), 3, 0x04, ModbusFrame::readResponseLength(2));

    for (uint8_t b : reply)
    {
        frame.push(b);
    }

    TEST_ASSERT_FALSE(frame.complete());
    TEST_ASSERT_TRUE(frame.remaining() > 0);
}

void test_bytes_after_completion_are_ignored()
{
    std::vector<uint8_t> reply = makeReply(1, 0x03, {0xAAAA});
    uint8_t buf[256];
    ModbusFrame::Assembler frame(buf, sizeof(buf), 1, 0x03, ModbusFrame::readResponseLength(1));

    for (uint8_t b : reply)
    {
        frame.push(b);
    }
    TEST_ASSERT_TRUE(frame.push(0x55));
    TEST_ASSERT_EQUAL_UINT(reply.size(), frame.length());
    TEST_ASSERT_EQUAL_UINT(0, frame.remaining());
}

void test_never_writes_past_buffer()
{
    uint8_t buf[8];
    // Expected length larger than the buffer is clamped to it.
    ModbusFrame::Assembler frame(buf, sizeof(buf), 1, 0x03, 100);
    frame.push(0x01);
    frame.push(0x03);
    for (int i = 0; i < 50; i++)
    {
        frame.push(0x00);
    }
    TEST_ASSERT_TRUE(frame.length() <= sizeof(buf));
    TEST_ASSERT_TRUE(frame.complete());
}

void setup()
{
    UNITY_BEGIN();

    RUN_TEST(test_crc_known_vector);
    RUN_TEST(test_crc_rejects_corruption_and_short_input);
    RUN_TEST(test_response_length);
    RUN_TEST(test_completes_exactly_at_expected_length);
    RUN_TEST(test_discards_stray_bytes_before_header);
    RUN_TEST(test_discards_stray_byte_equal_to_slave_id);
    RUN_TEST(test_exception_reply_completes_at_five_bytes);
    RUN_TEST(test_truncated_reply_stays_incomplete);
    RUN_TEST(test_reply_from_other_slave_is_ignored);
    RUN_TEST(test_bytes_after_completion_are_ignored);
    RUN_TEST(test_never_writes_past_buffer);

    UNITY_END();
}

void loop()
{
    // Empty - tests run once in setup()
}
