#include <gtest/gtest.h>

#include "utils/byte_utils.hpp"

#include <cstring>

// ---- u16 ----

TEST(ByteUtils, U16Roundtrip) {
    uint8_t buf[2]{};
    utils::write_u16_le(buf, 0x1234);
    EXPECT_EQ(utils::read_u16_le(buf), 0x1234);
}

TEST(ByteUtils, U16LittleEndianLayout) {
    uint8_t buf[2]{};
    utils::write_u16_le(buf, 0xABCD);
    EXPECT_EQ(buf[0], 0xCD); // low byte first
    EXPECT_EQ(buf[1], 0xAB);
}

TEST(ByteUtils, U16Zero) {
    uint8_t buf[2]{0xFF, 0xFF};
    utils::write_u16_le(buf, 0);
    EXPECT_EQ(utils::read_u16_le(buf), 0u);
}

TEST(ByteUtils, U16Max) {
    uint8_t buf[2]{};
    utils::write_u16_le(buf, UINT16_MAX);
    EXPECT_EQ(utils::read_u16_le(buf), UINT16_MAX);
}

// ---- u32 ----

TEST(ByteUtils, U32Roundtrip) {
    uint8_t buf[4]{};
    utils::write_u32_le(buf, 0xDEADBEEF);
    EXPECT_EQ(utils::read_u32_le(buf), 0xDEADBEEFu);
}

TEST(ByteUtils, U32LittleEndianLayout) {
    uint8_t buf[4]{};
    utils::write_u32_le(buf, 0x01020304);
    EXPECT_EQ(buf[0], 0x04);
    EXPECT_EQ(buf[1], 0x03);
    EXPECT_EQ(buf[2], 0x02);
    EXPECT_EQ(buf[3], 0x01);
}

TEST(ByteUtils, U32Zero) {
    uint8_t buf[4]{0xFF, 0xFF, 0xFF, 0xFF};
    utils::write_u32_le(buf, 0);
    EXPECT_EQ(utils::read_u32_le(buf), 0u);
}

TEST(ByteUtils, U32Max) {
    uint8_t buf[4]{};
    utils::write_u32_le(buf, UINT32_MAX);
    EXPECT_EQ(utils::read_u32_le(buf), UINT32_MAX);
}

// ---- u64 ----

TEST(ByteUtils, U64Roundtrip) {
    uint8_t buf[8]{};
    utils::write_u64_le(buf, 0x0102030405060708ULL);
    EXPECT_EQ(utils::read_u64_le(buf), 0x0102030405060708ULL);
}

TEST(ByteUtils, U64LittleEndianLayout) {
    uint8_t buf[8]{};
    utils::write_u64_le(buf, 0x0102030405060708ULL);
    EXPECT_EQ(buf[0], 0x08);
    EXPECT_EQ(buf[1], 0x07);
    EXPECT_EQ(buf[2], 0x06);
    EXPECT_EQ(buf[3], 0x05);
    EXPECT_EQ(buf[4], 0x04);
    EXPECT_EQ(buf[5], 0x03);
    EXPECT_EQ(buf[6], 0x02);
    EXPECT_EQ(buf[7], 0x01);
}

TEST(ByteUtils, U64Zero) {
    uint8_t buf[8];
    std::memset(buf, 0xFF, sizeof(buf));
    utils::write_u64_le(buf, 0);
    EXPECT_EQ(utils::read_u64_le(buf), 0u);
}

TEST(ByteUtils, U64Max) {
    uint8_t buf[8]{};
    utils::write_u64_le(buf, UINT64_MAX);
    EXPECT_EQ(utils::read_u64_le(buf), UINT64_MAX);
}

// ---- Adjacent writes don't clobber ----

TEST(ByteUtils, AdjacentWrites) {
    uint8_t buf[16]{};
    utils::write_u32_le(buf, 0xAAAAAAAA);
    utils::write_u32_le(buf + 4, 0xBBBBBBBB);
    utils::write_u64_le(buf + 8, 0xCCCCCCCCDDDDDDDDULL);

    EXPECT_EQ(utils::read_u32_le(buf), 0xAAAAAAAAu);
    EXPECT_EQ(utils::read_u32_le(buf + 4), 0xBBBBBBBBu);
    EXPECT_EQ(utils::read_u64_le(buf + 8), 0xCCCCCCCCDDDDDDDDULL);
}
