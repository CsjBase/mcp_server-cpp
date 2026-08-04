#include "utils/zlib_compress.h"

#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <vector>

using utils::ZlibCompress;

// ---- round-trip ----
TEST(ZlibCompressTest, RoundTripSmall)
{
    ZlibCompress c;
    std::string input = "Hello, World!";

    c.reset_stream();
    size_t bound = c.compressed_bound(input.size());
    std::vector<char> compressed(bound);
    size_t out_len = c.compress(input.data(), input.size(), compressed.data(), bound);
    ASSERT_GT(out_len, 0u);

    std::string decompressed = c.decompress(compressed.data(), out_len);
    EXPECT_EQ(decompressed, input);
}

TEST(ZlibCompressTest, RoundTripEmpty)
{
    ZlibCompress c;
    std::string input;

    c.reset_stream();
    size_t bound = c.compressed_bound(input.size());
    std::vector<char> compressed(bound);
    size_t out_len = c.compress(input.data(), input.size(), compressed.data(), bound);
    // 空输入也应产生非零输出（zlib 头 + 尾）
    ASSERT_GT(out_len, 0u);

    std::string decompressed = c.decompress(compressed.data(), out_len);
    EXPECT_EQ(decompressed, input);
}

TEST(ZlibCompressTest, RoundTripLarge)
{
    ZlibCompress c;
    // 生成可压缩数据（重复模式）
    std::string input;
    for (int i = 0; i < 1000; ++i)
        input += "The quick brown fox jumps over the lazy dog. ";

    c.reset_stream();
    size_t bound = c.compressed_bound(input.size());
    std::vector<char> compressed(bound);
    size_t out_len = c.compress(input.data(), input.size(), compressed.data(), bound);
    ASSERT_GT(out_len, 0u);
    // 可压缩数据应显著小于原始大小
    EXPECT_LT(out_len, input.size());

    std::string decompressed = c.decompress(compressed.data(), out_len);
    EXPECT_EQ(decompressed, input);
}

TEST(ZlibCompressTest, RoundTripBinary)
{
    ZlibCompress c;
    // 不可压缩的随机数据
    std::vector<char> input(4096);
    for (size_t i = 0; i < input.size(); ++i)
        input[i] = static_cast<char>(i * 7 + 13);

    c.reset_stream();
    size_t bound = c.compressed_bound(input.size());
    std::vector<char> compressed(bound);
    size_t out_len = c.compress(input.data(), input.size(), compressed.data(), bound);
    ASSERT_GT(out_len, 0u);

    std::string decompressed = c.decompress(compressed.data(), out_len);
    ASSERT_EQ(decompressed.size(), input.size());
    EXPECT_EQ(std::memcmp(decompressed.data(), input.data(), input.size()), 0);
}

// ---- compressed_bound ----
TEST(ZlibCompressTest, CompressedBoundIsSufficient)
{
    ZlibCompress c;
    std::vector<size_t> sizes = {0, 1, 16, 256, 4096, 65536};
    for (auto sz : sizes)
    {
        EXPECT_GE(c.compressed_bound(sz), sz) << "bound too small for size " << sz;
    }
}

// ---- reset_stream ----
TEST(ZlibCompressTest, ResetStreamAllowsReuse)
{
    ZlibCompress c;
    std::string input1 = "First message.";
    std::string input2 = "Second message, different content.";

    // First compression
    c.reset_stream();
    size_t bound = c.compressed_bound(input1.size());
    std::vector<char> out1(bound);
    size_t len1 = c.compress(input1.data(), input1.size(), out1.data(), bound);
    ASSERT_GT(len1, 0u);

    // Reset and second compression
    c.reset_stream();
    bound = c.compressed_bound(input2.size());
    std::vector<char> out2(bound);
    size_t len2 = c.compress(input2.data(), input2.size(), out2.data(), bound);
    ASSERT_GT(len2, 0u);

    // Both should decompress correctly
    EXPECT_EQ(c.decompress(out1.data(), len1), input1);
    EXPECT_EQ(c.decompress(out2.data(), len2), input2);
}

// ---- decompress invalid data ----
TEST(ZlibCompressTest, DecompressGarbageReturnsEmpty)
{
    ZlibCompress c;
    // Not zlib-compressed data
    std::string garbage = "this is not compressed data at all";
    std::string result = c.decompress(garbage.data(), garbage.size());
    EXPECT_TRUE(result.empty());
}

// ---- single-byte round-trip ----
TEST(ZlibCompressTest, RoundTripSingleByte)
{
    ZlibCompress c;
    std::string input = "A";

    c.reset_stream();
    size_t bound = c.compressed_bound(input.size());
    std::vector<char> compressed(bound);
    size_t out_len = c.compress(input.data(), input.size(), compressed.data(), bound);
    ASSERT_GT(out_len, 0u);

    std::string decompressed = c.decompress(compressed.data(), out_len);
    EXPECT_EQ(decompressed, input);
}
