#include "logger/rotater/CompressHandler.h"
#include "logger/rotater/EncryptHandler.h"
#include "logger/public.h"
#include "utils/aes_crypt.h"
#include "utils/zlib_compress.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace
{

class LogToolTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        tmpdir_ = "/tmp/log_tool_test";
        std::filesystem::remove_all(tmpdir_);
        std::filesystem::create_directories(tmpdir_);
    }

    void TearDown() override
    {
        std::filesystem::remove_all(tmpdir_);
    }

    std::string path(const std::string &name) const { return tmpdir_ + "/" + name; }

    void write_file(const std::string &filepath, const std::string &content)
    {
        std::ofstream out(filepath, std::ios::binary);
        out.write(content.data(), static_cast<std::streamsize>(content.size()));
    }

    std::string read_file(const std::string &filepath) const
    {
        std::ifstream in(filepath, std::ios::binary);
        return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    }

    bool file_exists(const std::string &filepath) const
    {
        return std::filesystem::exists(filepath);
    }

    size_t file_size(const std::string &filepath) const
    {
        return static_cast<size_t>(std::filesystem::file_size(filepath));
    }

    std::string tmpdir_;
};

// ---- compress / decompress (log_tool 内部使用 ZlibCompress) ----
TEST_F(LogToolTest, CompressDecompressRoundTrip)
{
    std::string content = "Test log content for compression round-trip.\n";
    write_file(path("test.log"), content);

    // compress
    auto zlib = std::make_unique<utils::ZlibCompress>();
    zlib->reset_stream();
    auto input = read_file(path("test.log"));
    size_t bound = zlib->compressed_bound(input.size());
    std::vector<char> out(bound);
    size_t out_len = zlib->compress(input.data(), input.size(), out.data(), bound);
    ASSERT_GT(out_len, 0u);
    write_file(path("test.log.gz"), std::string(out.data(), out_len));

    EXPECT_TRUE(file_exists(path("test.log.gz")));
    EXPECT_GT(file_size(path("test.log.gz")), 0u);

    // decompress
    auto zlib2 = std::make_unique<utils::ZlibCompress>();
    zlib2->reset_stream();
    auto compressed = read_file(path("test.log.gz"));
    auto decompressed = zlib2->decompress(compressed.data(), compressed.size());
    ASSERT_FALSE(decompressed.empty());

    EXPECT_EQ(decompressed, content);
}

TEST_F(LogToolTest, CompressEmptyFile)
{
    write_file(path("empty.log"), "");

    auto zlib = std::make_unique<utils::ZlibCompress>();
    zlib->reset_stream();
    auto input = read_file(path("empty.log"));
    size_t bound = zlib->compressed_bound(input.size());
    std::vector<char> out(bound);
    size_t out_len = zlib->compress(input.data(), input.size(), out.data(), bound);
    ASSERT_GT(out_len, 0u); // zlib header+footer even for empty input

    write_file(path("empty.log.gz"), std::string(out.data(), out_len));
    EXPECT_TRUE(file_exists(path("empty.log.gz")));

    // decompress back
    auto zlib2 = std::make_unique<utils::ZlibCompress>();
    zlib2->reset_stream();
    auto compressed = read_file(path("empty.log.gz"));
    auto decompressed = zlib2->decompress(compressed.data(), compressed.size());
    EXPECT_EQ(decompressed, "");
}

TEST_F(LogToolTest, CompressedOutputSmallerForRepeatingData)
{
    // 高度可压缩的数据
    std::string content(10000, 'A');
    write_file(path("repeating.log"), content);

    auto zlib = std::make_unique<utils::ZlibCompress>();
    zlib->reset_stream();
    auto input = read_file(path("repeating.log"));
    size_t bound = zlib->compressed_bound(input.size());
    std::vector<char> out(bound);
    size_t out_len = zlib->compress(input.data(), input.size(), out.data(), bound);
    ASSERT_GT(out_len, 0u);

    EXPECT_LT(out_len, content.size() / 10) << "Repeating data should compress significantly";
}

// ---- encrypt / decrypt (log_tool 内部使用 EncryptHandler 静态方法) ----
TEST_F(LogToolTest, EncryptDecryptRoundTrip)
{
    std::string content = "Sensitive log data: password=secret123\n";
    write_file(path("secret.log"), content);

    auto crypt = utils::AesCrypt("my-password");
    logger::EncryptHandler::encrypt_file(path("secret.log"), path("secret.log.enc"), crypt);

    EXPECT_TRUE(file_exists(path("secret.log.enc")));
    EXPECT_NE(read_file(path("secret.log.enc")), content);

    // decrypt
    auto crypt2 = utils::AesCrypt("my-password");
    logger::EncryptHandler::decrypt_file(path("secret.log.enc"), path("secret_decrypted.log"), crypt2);

    EXPECT_EQ(read_file(path("secret_decrypted.log")), content);
}

TEST_F(LogToolTest, EncryptWrongKeyFails)
{
    std::string content = "Top secret data\n";
    write_file(path("top_secret.log"), content);

    auto crypt = utils::AesCrypt("correct-key");
    logger::EncryptHandler::encrypt_file(path("top_secret.log"), path("top_secret.log.enc"), crypt);

    auto wrong_crypt = utils::AesCrypt("wrong-key");
    EXPECT_THROW({
        logger::EncryptHandler::decrypt_file(path("top_secret.log.enc"),
                                             path("top_secret_decrypted.log"), wrong_crypt);
    }, logger::LogException);
}

TEST_F(LogToolTest, EncryptSmallFile)
{
    write_file(path("small.log"), "x");

    auto crypt = utils::AesCrypt("key");
    logger::EncryptHandler::encrypt_file(path("small.log"), path("small.log.enc"), crypt);

    EXPECT_TRUE(file_exists(path("small.log.enc")));

    auto crypt2 = utils::AesCrypt("key");
    logger::EncryptHandler::decrypt_file(path("small.log.enc"), path("small_decrypted.log"), crypt2);
    EXPECT_TRUE(file_exists(path("small_decrypted.log")));
    EXPECT_EQ(read_file(path("small_decrypted.log")), "x");
}

TEST_F(LogToolTest, EncryptBinaryFile)
{
    std::string content(4096, '\0');
    for (size_t i = 0; i < content.size(); ++i)
        content[i] = static_cast<char>(i % 256);
    write_file(path("binary.bin"), content);

    auto crypt = utils::AesCrypt("binary-key");
    logger::EncryptHandler::encrypt_file(path("binary.bin"), path("binary.bin.enc"), crypt);

    auto crypt2 = utils::AesCrypt("binary-key");
    logger::EncryptHandler::decrypt_file(path("binary.bin.enc"), path("binary_decrypted.bin"), crypt2);

    EXPECT_EQ(read_file(path("binary_decrypted.bin")), content);
}

// ---- pipeline: compress then encrypt (log_tool 两条命令串联) ----
TEST_F(LogToolTest, CompressThenEncryptPipeline)
{
    std::string content(1000, 'B');
    write_file(path("pipeline.log"), content);

    // Step 1: compress
    auto zlib = std::make_unique<utils::ZlibCompress>();
    zlib->reset_stream();
    auto input = read_file(path("pipeline.log"));
    size_t bound = zlib->compressed_bound(input.size());
    std::vector<char> out(bound);
    size_t out_len = zlib->compress(input.data(), input.size(), out.data(), bound);
    ASSERT_GT(out_len, 0u);
    write_file(path("pipeline.log.gz"), std::string(out.data(), out_len));

    // Step 2: encrypt
    auto crypt = utils::AesCrypt("pipeline-key");
    logger::EncryptHandler::encrypt_file(path("pipeline.log.gz"), path("pipeline.log.gz.enc"), crypt);

    EXPECT_TRUE(file_exists(path("pipeline.log.gz.enc")));

    // Reverse: decrypt
    auto crypt2 = utils::AesCrypt("pipeline-key");
    logger::EncryptHandler::decrypt_file(path("pipeline.log.gz.enc"), path("pipeline_decrypted.log.gz"), crypt2);

    // Reverse: decompress
    auto zlib2 = std::make_unique<utils::ZlibCompress>();
    zlib2->reset_stream();
    auto compressed = read_file(path("pipeline_decrypted.log.gz"));
    auto decompressed = zlib2->decompress(compressed.data(), compressed.size());
    ASSERT_FALSE(decompressed.empty());

    EXPECT_EQ(decompressed, content);
}

// ---- corrupted file handling ----
TEST_F(LogToolTest, DecompressGarbageFile)
{
    std::string garbage = "not a gzip file at all, just random bytes";
    write_file(path("garbage.gz"), garbage);

    auto zlib = std::make_unique<utils::ZlibCompress>();
    zlib->reset_stream();
    auto data = read_file(path("garbage.gz"));
    auto result = zlib->decompress(data.data(), data.size());
    EXPECT_TRUE(result.empty());
}

} // namespace
